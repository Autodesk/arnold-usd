//
// SPDX-License-Identifier: Apache-2.0
//

#include "asset_utils.h"

// Asset API was added in Arnold 7.4.5.0
#if ARNOLD_VERSION_NUM >= 70405

#include <pxr/pxr.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdUtils/dependencies.h>
#include <algorithm>
#include <sstream>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

typedef std::unordered_map<std::string, std::pair<std::string, std::string>> SeenReferenceMap;

/**
 * A wrapper over the AiBegin/AiEnd calls to follow the RAII technique,
 * and close the session when the object goes out of scope.
 * Also makes sure we open the session only if needed.
 */
class ArnoldSession
{
public:
    ArnoldSession()
    {
        if (!AiArnoldIsActive())
        {
            AiBegin();
            m_ownArnoldSession = true;
        }
    }

    ~ArnoldSession()
    {
        if (m_ownArnoldSession)
            AiEnd();
    }

private:
    bool m_ownArnoldSession = false;
};

/**
 * Helper function to check if the given prim
 * is a specific Arnold shader.
 */
inline bool IsArnoldShader(const SdfPrimSpecHandle& prim, const TfToken& shaderType)
{
    if (!prim)
        return false;
    if (shaderType.IsEmpty())
        return false;
    if (prim->GetTypeName().GetString() != "Shader")
        return false;

    SdfAttributeSpecHandle idAttr = prim->GetAttributes().get(TfToken("info:id"));
    if (!idAttr || !idAttr->GetDefaultValue().IsHolding<TfToken>())
        return false;

    const TfToken& idToken = idAttr->GetDefaultValue().Get<TfToken>();
    return idToken == TfToken(std::string("arnold:") + shaderType.GetString());
}

/**
 * Returns true if the given Arnold node parameter is a 'path' type parameter.
 */
inline bool IsArnoldPathParameter(const AtNodeEntry* nentry, const AtParamEntry* pentry)
{
    if (nentry == nullptr || pentry == nullptr)
        return false;

    // must be a string
    if (AiParamGetType(pentry) != AI_TYPE_STRING)
        return false;

    // path type parameters define the `path = "file"` metadata
    AtString pathMetaData;
    if (AiMetaDataGetStr(nentry, AiParamGetName(pentry), AtString("path"), &pathMetaData) &&
        pathMetaData == AtString("file"))
        return true;

    // OSL shaders define the `widget = "filename"` metadata
    AtString widget;
    if (AiMetaDataGetStr(nentry, AiParamGetName(pentry), AtString("widget"), &widget) && widget == AtString("filename"))
        return true;

    return false;
}

/**
 * Converts an absolute path to a path relative to the scene file.
 */
inline std::string ComputeRelativePathToRoot(UsdStageRefPtr stage, const std::string& absPath)
{
    std::string rootLayerPath = stage->GetRootLayer()->GetRealPath();
    std::string rootDir = TfGetPathName(rootLayerPath);

    std::string relative;
    // This is a basic implementation replacing std::filesystem::relative().
    // We assume that the paths are normalized absolute paths (no '.' or '..')
    // and just cut the root folder.
    {
        // normalize the paths
        std::string absPathNorm = TfNormPath(absPath);
        std::string rootDirNorm = TfNormPath(rootDir);
#ifdef WIN32
        // Windows is not case-sensitive, therefore we convert paths to lower case
        // before comparing them as strings
        std::transform(absPathNorm.begin(), absPathNorm.end(), absPathNorm.begin(), ::tolower);
        std::transform(rootDirNorm.begin(), rootDirNorm.end(), rootDirNorm.begin(), ::tolower);
#endif
        // add trailing '/' to root dir
        if (rootDirNorm.back() != '/')
            rootDirNorm += "/";

        // check if our path is under the root folder
        if (absPathNorm.find(rootDirNorm) == 0)
        {
            // make the path relative to the root folder
            relative = absPath.substr(rootDir.length());
            // always use forward-slashes in the returned relative path
            std::replace(relative.begin(), relative.end(), '\\', '/');
        }
    }

    return relative;
}

/**
 * Adds the given dependency to our list.
 */
inline void AddDependency(const std::string& ref, USDDependency::Type type,
    const SdfPath& primPath, const TfToken& primTypeName, const SdfPath& attribute,
    std::vector<USDDependency>& dependencies, UsdStageRefPtr stage, 
    const SdfLayerHandle& layer, ArResolver& resolver, 
    SeenReferenceMap& seenReferences)
{
    if (ref.empty())
        return;

    std::string anchoredPath;
    std::string resolvedPath;

    // the reference was already processed, use the resolved paths
    if (seenReferences.find(ref) != seenReferences.end())
    {
        auto refPaths = seenReferences[ref];
        anchoredPath = refPaths.first;
        resolvedPath = refPaths.second;
    }
    else
    {
        // resolve the reference to an absolute path
        std::string relRef = SdfComputeAssetPathRelativeToLayer(layer, ref);
        resolvedPath = resolver.Resolve(relRef);
        // convert a relative reference relative to the main scene
        anchoredPath = ref;
        if (!resolvedPath.empty() && TfIsRelativePath(ref))
        {
            std::string relativeToRoot = ComputeRelativePathToRoot(stage, resolvedPath);
            // convert only if the file is located under the root folder
            if (!relativeToRoot.empty() && relativeToRoot.at(0) != '.')
                anchoredPath = relativeToRoot;
        }

        seenReferences[ref] = std::make_pair(anchoredPath, resolvedPath);
    }

    // create a dependency
    dependencies.push_back(USDDependency(type, anchoredPath,
        resolvedPath, layer, primPath, primTypeName, attribute));
}

/**
 * Helper function to read the value of a prim attribute
 * and add it as a file dependency. Handles time samples.
 */
template <typename T>
inline void CollectAttrDependencies(
    const SdfAttributeSpecHandle& attr,
    const SdfLayerHandle& layer,
    const std::function<void(const T&)>& process_func)
{
    // collect attribute value
    VtValue defaultVal = attr->GetDefaultValue();
    if (defaultVal.IsHolding<T>())
    {
        T value = defaultVal.UncheckedGet<T>();
        process_func(value);
    }
        
    // collect time samples
    std::set<double> timeSamples = layer->ListTimeSamplesForPath(attr->GetPath());
    for (double t : timeSamples)
    {
        T value_t;
        if (layer->QueryTimeSample(attr->GetPath(), t, &value_t))
        {
            process_func(value_t);
        }
    }
}

/**
 * Returns all dependencies of an Arnold OSL shader node.
 * 
 * To be able to tell if an OSL shader parameter refers to a file,
 * we need to load the OSL code into an Arnold shader
 * and check the meta data of the node parameters.
 */
inline void CollectOslShaderDependencies(
    UsdStageRefPtr stage,
    const SdfLayerHandle& layer,
    const SdfPrimSpecHandle& prim,
    std::vector<USDDependency>& dependencies,
    SeenReferenceMap& seenReferences,
    ArResolver& resolver)
{
    // read the osl shader code
    SdfAttributeSpecHandle codeAttr = prim->GetAttributes().get(TfToken("inputs:code"));
    if (!codeAttr || !codeAttr->GetDefaultValue().IsHolding<std::string>())
        return;

    const std::string& code = codeAttr->GetDefaultValue().Get<std::string>();
    if (code.empty())
        return;

    // load the OSL shader in an Arnold universe
    AtUniverse* universe = AiUniverse();
    if (universe == nullptr)
    {
        AiMsgError("[usd] Failed to create Arnold universe");
        return;
    }
    AtNode* osl = AiNode(universe, AtString("osl"), AtString("osl_tmp"));
    if (osl == nullptr)
    {
        AiMsgError("[usd] Failed to create Arnold sl shader node");
        AiUniverseDestroy(universe);
        return;
    }
    AiNodeSetStr(osl, AtString("code"), AtString(code.c_str()));

    // find path type parameters
    const AtNodeEntry* nentry = AiNodeGetNodeEntry(osl);
    AtParamIterator* piter = AiNodeEntryGetParamIterator(nentry);
    while (!AiParamIteratorFinished(piter))
    {
        const AtParamEntry* pentry = AiParamIteratorGetNext(piter);
        if (IsArnoldPathParameter(nentry, pentry))
        {
            // read the parameter value from the USD prim
            SdfAttributeSpecHandle pathAttr = prim->GetAttributes().get(TfToken(std::string("inputs:") + AiParamGetName(pentry).c_str()));
            if (!pathAttr || !pathAttr->GetDefaultValue().IsHolding<std::string>())
                continue;

            CollectAttrDependencies<std::string>(pathAttr, layer,
                [&](const std::string& val) {
                    AddDependency(val, USDDependency::Type::Attribute, 
                        prim->GetPath(), prim->GetTypeName(), pathAttr->GetPath(),
                        dependencies, stage, layer, resolver, seenReferences);
                });
        }
    }
    AiParamIteratorDestroy(piter);

    // cleanup
    AiUniverseDestroy(universe);
}

/**
 * Helper function to iterate over all prims in a layer.
 */
inline void TraversePrimSpecs(
    const SdfPrimSpecHandle& prim,
    const std::function<void(const SdfPrimSpecHandle&)>& fn)
{
    fn(prim);

    for (const auto& child : prim->GetNameChildren())
        TraversePrimSpecs(child, fn);
}

/**
 * Helper function to iterate over all prims in a layer.
 */
inline void TraverseLayer(const SdfLayerHandle& layer,
                   const std::function<void(const SdfPrimSpecHandle&)>& fn)
{
    SdfPrimSpecHandle root = layer->GetPseudoRoot();

    for (const auto& prim : root->GetNameChildren())
        TraversePrimSpecs(prim, fn);
}

/**
 * Returns all dependencies found in the given layer.
 */
inline void CollectDependenciesFromLayer(
    UsdStageRefPtr stage,
    const SdfLayerHandle& layer,
    std::vector<USDDependency>& dependencies,
    SeenReferenceMap& seenReferences,
    ArResolver& resolver)
{
    if (!layer)
        return;

    // collect sublayers
    for (const std::string& sub : layer->GetSubLayerPaths())
    {
        AddDependency(sub, USDDependency::Type::Sublayer, SdfPath(), TfToken(), SdfPath(),
            dependencies, stage, layer, resolver, seenReferences);
    }

    // iterate all prims in this layer
    TraverseLayer(layer, [&](const SdfPrimSpecHandle& prim) {

        // collect dependencies from attributes
        for (const SdfAttributeSpecHandle& attr : prim->GetAttributes())
        {
            if (!attr)
                continue;

            const std::string attrName = attr->GetName();

            // asset type attribute
            if (attr->GetTypeName() == SdfValueTypeNames->Asset)
            {
                CollectAttrDependencies<SdfAssetPath>(attr, layer,
                    [&](const SdfAssetPath& val) {
                        AddDependency(val.GetAssetPath(), USDDependency::Type::Attribute, 
                            prim->GetPath(), prim->GetTypeName(), attr->GetPath(),
                            dependencies, stage, layer, resolver, seenReferences);
                    });
            }

            // asset array type attribute
            else if (attr->GetTypeName() == SdfValueTypeNames->AssetArray)
            {
                CollectAttrDependencies<VtArray<SdfAssetPath>>(attr, layer,
                    [&](const VtArray<SdfAssetPath>& arr) {
                        for (SdfAssetPath val : arr)
                        {
                            AddDependency(val.GetAssetPath(), USDDependency::Type::Attribute,
                                prim->GetPath(), prim->GetTypeName(), attr->GetPath(),
                                dependencies, stage, layer, resolver, seenReferences);
                        }
                    });
            }

            // NOTE filename in ArnoldUsd is a string type not an asset type
            // therefore it needs special care
            if (attrName == "arnold:filename" && prim->GetTypeName().GetString() == "ArnoldUsd")
            {
                CollectAttrDependencies<std::string>(attr, layer,
                    [&](const std::string& val) {
                        AddDependency(val, USDDependency::Type::Attribute, 
                            prim->GetPath(), prim->GetTypeName(), attr->GetPath(),
                            dependencies, stage, layer, resolver, seenReferences);
                    });
            }
        }

        // collect dependencies from Arnold OSL shader
        if (IsArnoldShader(prim, TfToken("osl")))
            CollectOslShaderDependencies(stage, layer, prim, dependencies, seenReferences, resolver);
        
        // collect references
        const auto refList = prim->GetReferenceList();
        SdfReferenceVector refs;
        {
            const auto prependedItems = refList.GetPrependedItems();
            const auto appendedItems = refList.GetAppendedItems();
            const auto addedItems = refList.GetAddedItems();
            const auto explicitItems = refList.GetExplicitItems();

            // combine all authored list-op opinions
            refs.insert(refs.end(), prependedItems.begin(), prependedItems.end());
            refs.insert(refs.end(), appendedItems.begin(), appendedItems.end());
            refs.insert(refs.end(), addedItems.begin(), addedItems.end());
            refs.insert(refs.end(), explicitItems.begin(), explicitItems.end());
        }
        for (const SdfReference& ref : refs)
        {
            AddDependency(ref.GetAssetPath(), USDDependency::Type::Reference, 
                prim->GetPath(), prim->GetTypeName(), SdfPath(),
                dependencies, stage, layer, resolver, seenReferences);
        }

        // collect payloads
        const auto payloadList = prim->GetPayloadList();
        SdfPayloadVector payloads;
        {
            const auto prependedItems = payloadList.GetPrependedItems();
            const auto appendedItems = payloadList.GetAppendedItems();
            const auto addedItems = payloadList.GetAddedItems();
            const auto explicitItems = payloadList.GetExplicitItems();
            // combine all authored list-op opinions
            payloads.insert(payloads.end(), prependedItems.begin(), prependedItems.end());
            payloads.insert(payloads.end(), appendedItems.begin(), appendedItems.end());
            payloads.insert(payloads.end(), addedItems.begin(), addedItems.end());
            payloads.insert(payloads.end(), explicitItems.begin(), explicitItems.end());
        }
        for (const SdfPayload& p : payloads)
        {
            AddDependency(p.GetAssetPath(), USDDependency::Type::Payload,
                prim->GetPath(), prim->GetTypeName(), SdfPath(),
                dependencies, stage, layer, resolver, seenReferences);
        }
    });
}

/**
 * Returns all dependencies found in a USD scene.
 *
 * The function iterates over all prims in all used layers
 * and collects dependencies defined in asset type attributes.
 * Also collects sublayers, references and payloads.
 */
std::vector<USDDependency> CollectDependencies(UsdStageRefPtr stage)
{
    std::vector<USDDependency> dependencies;

    ArResolver& resolver = ArGetResolver();
    SeenReferenceMap seenReferences;

    // collect dependencies from all used layers
    SdfLayerHandleVector usedLayers = stage->GetUsedLayers();
    for (auto &layer : usedLayers)
    {
        CollectDependenciesFromLayer(stage, layer, dependencies,
            seenReferences, resolver);
    }

    return dependencies;
}

/**
 * Helper function to convert an std::string to AtString.
 */
inline AtString StdToAtString(const std::string& str)
{
    return !str.empty() ? AtString(str.c_str()) : AtString();
}

/**
 * Helper function to define the Arnold file type based on the dependency.
 */
inline AtFileType GetArnoldFileTypeFromDependency(const USDDependency& dep)
{
    // Set Procedural type for an Arnold Procedural scene file.
    // This tells Arnold to collect assets from this scene file.
    if (dep.type == USDDependency::Type::Attribute && dep.attribute.GetName() == "arnold:filename")
    {
       if (dep.primTypeName == TfToken("ArnoldProcedural") || dep.primTypeName == TfToken("ArnoldUsd")
          || dep.primTypeName == TfToken("ArnoldAlembic"))
          return AtFileType::Procedural;
    }

    // We consider dependencies defined by prim attributes as 'Asset' type,
    // thus they can be resolved using the Arnold asset search path.
    //
    // TODO This is not entirely correct, because not all of these dependencies
    // might be translated as Arnold assets.
    // We would need to explicitly define which dependency is an Arnold asset.
    return dep.type == USDDependency::Type::Attribute ? AtFileType::Asset : AtFileType::Custom;
}

/**
 * Helper function to define the node name of an AtAsset reference.
 * This is the node that defines the asset in an Arnold scene, 
 * but of course node is an Arnold term and needs to be interpreted
 * differently in a USD scene based on the dependency.
 */
inline std::string GetNodeNameFromDependency(const USDDependency& dep)
{
    switch (dep.type)
    {
        // if the dependency comes from a prim (attribute, reference, payload)
        // we use the prim path
        case USDDependency::Type::Attribute:
        case USDDependency::Type::Reference:
        case USDDependency::Type::Payload:
            return dep.primPath.GetString();
        // otherwise (sublayer)
        // we use the layer name
        default:
            return dep.layer->GetDisplayName();
    }
}

/**
 * Helper function to define the node parameter of an AtAsset reference.
 * This is the node parameter that defines the asset in an Arnold scene, 
 * but of course node is an Arnold term and needs to be interpreted
 * differently in a USD scene based on the dependency.
 */
inline std::string GetNodeParameterFromDependency(const USDDependency& dep)
{
    switch (dep.type)
    {
        // if the dependency comes from a prim attribute
        // we use the attribute name
        case USDDependency::Type::Attribute: 
        {
            SdfAttributeSpecHandle attr = dep.layer->GetAttributeAtPath(dep.attribute);
            return attr ? attr->GetName() : std::string();
        }
        // if the dependency is a sublayer, reference or payload
        // we use a specific string
        case USDDependency::Type::Sublayer: return "sublayer";
        case USDDependency::Type::Reference: return "reference";
        case USDDependency::Type::Payload: return "payload";
        // return empty string for everything else
        default: return std::string();
    }
}

/**
 * Helper function to define if an asset needs to be ignored,
 * if the file is missing. Typically Arnold image nodes
 * define this flag, called 'ignore_missing_textures'.
 */
inline bool GetIgnoreMissingFromDependency(const USDDependency& dep)
{
    // Arnold image shader
    if (dep.type == USDDependency::Type::Attribute && dep.attribute.GetName() == "inputs:filename" && dep.layer)
    {
        SdfPrimSpecHandle prim = dep.layer->GetPrimAtPath(dep.primPath);
        if (IsArnoldShader(prim, TfToken("image")))
        {
            SdfAttributeSpecHandle ignoreMissingAttr = prim->GetAttributes().get(TfToken("inputs:ignore_missing_textures"));
            return ignoreMissingAttr && ignoreMissingAttr->GetDefaultValue().Get<bool>();
        }
    }

    return false;
}

/**
 * Returns all assets found in the given USD scene.
 * 
 * The function collects all dependencies 
 * and converts them to Arnold assets.
 */
bool CollectSceneAssets(const std::string& filename, bool isProcedural, std::vector<AtAsset*>& assets)
{
    // open the scene file
    UsdStageRefPtr stage = UsdStage::Open(filename);

    if (!stage)
    {
        AiMsgError("Failed to open stage: %s", filename.c_str());
        return false;
    }

    // NOTE We need an open Arnold session to be able
    // to collect assets from OSL shaders.
    ArnoldSession arnoldSession;

    // collect dependencies from the USD scene
    std::vector<USDDependency> dependencies = CollectDependencies(stage);

    // if the scene is loaded as a procedural, we need to ignore the render settings
    // only cameras, lights, shapes, shaders and operators are permitted
    if (isProcedural)
    {
        std::vector<USDDependency> filtered;
        std::copy_if(dependencies.begin(), dependencies.end(), std::back_inserter(filtered),
            [](const USDDependency& dep) {
                return dep.primTypeName != TfToken("RenderSettings");
            });
        dependencies = std::move(filtered);
    }

    // log dependencies
    for (const USDDependency& dep : dependencies)
    {
        std::string src = GetNodeNameFromDependency(dep);        
        if (dep.type == USDDependency::Type::Attribute && !dep.attribute.IsEmpty())
            src = dep.attribute.GetString();
        AiMsgDebug("[usd] scene dependency: %s (ref: %s, type: %s, src: %s, layer: %s)",
            !dep.resolvedPath.empty() ? dep.resolvedPath.c_str() : dep.authoredPath.c_str(),
            dep.authoredPath.c_str(), USDDependency::GetTypeName(dep.type).c_str(), !src.empty() ? src.c_str() : "", 
            dep.layer->GetIdentifier().c_str());
    }

    // convert dependencies to assets
    for (const USDDependency& dep : dependencies)
    {
        if (dep.authoredPath.empty())
            continue;

        std::string resolvedPath = dep.resolvedPath;
        // if could not resolve the path, then use the scene reference
        // potentially these are paths that can be resolved by Arnold, like UDIM textures
        if (resolvedPath.empty())
            resolvedPath = dep.authoredPath;

        // add the dependency to the asset list
        AtAsset* asset = AiAsset(resolvedPath.c_str(), GetArnoldFileTypeFromDependency(dep));
        AiAssetSetIgnoreMissing(asset, GetIgnoreMissingFromDependency(dep));
        AiAssetAddReference(asset, StdToAtString(dep.authoredPath),
            StdToAtString(GetNodeNameFromDependency(dep)),
            StdToAtString(GetNodeParameterFromDependency(dep)));
        assets.push_back(asset);
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // Asset API

