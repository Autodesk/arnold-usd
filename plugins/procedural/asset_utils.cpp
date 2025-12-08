//
// SPDX-License-Identifier: Apache-2.0
//

#include "asset_utils.h"

#include <filesystem>
#include <pxr/pxr.h>
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

PXR_NAMESPACE_OPEN_SCOPE

/**
 * Converts an absolute path to a path relative to the scene file.
 */
inline std::string ComputeRelativePathToRoot(UsdStageRefPtr stage, const std::string& absPath)
{
    std::filesystem::path rootLayerPath = stage->GetRootLayer()->GetRealPath();
    std::filesystem::path rootDir = rootLayerPath.parent_path();
    std::filesystem::path relative = std::filesystem::relative(std::filesystem::path(absPath), rootDir);
    return relative.generic_string(); // always forward-slashes
}

/**
 * Adds the given dependency to our list.
 */
inline void AddDependency(const std::string& ref, const std::string& depType,
    const std::string& primPath, const std::string& attribute,
    std::vector<USDDependency>& dependencies, UsdStageRefPtr stage, 
    const SdfLayerHandle& layer, ArResolver& resolver, 
    std::unordered_set<std::string>& seenReferences)
{
    if (ref.empty())
        return;

    if (seenReferences.find(ref) != seenReferences.end())
        return;

    // resolve the reference to an absolute path
    std::string anchoredPath = SdfComputeAssetPathRelativeToLayer(layer, ref);
    std::string resolvedPath = resolver.Resolve(anchoredPath);
    // convert a relative reference relative to the main scene
    std::string finalRefPath = ref;
    if (!resolvedPath.empty() && std::filesystem::path(ref).is_relative())
    {
        std::string relativeToRoot = ComputeRelativePathToRoot(stage, resolvedPath);
        // convert only if the file is located under the root folder
        if (!relativeToRoot.empty() && relativeToRoot.at(0) != '.')
            finalRefPath = relativeToRoot;
    }

    // create a dependency
    dependencies.push_back(USDDependency(depType, finalRefPath, 
        resolvedPath, layer->GetIdentifier(), primPath, attribute));
    seenReferences.insert(ref);
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
    std::unordered_set<std::string>& seenReferences,
    ArResolver& resolver)
{
    if (!layer)
        return;

    // collect sublayers
    for (const std::string& sub : layer->GetSubLayerPaths())
    {
        AddDependency(sub, "sublayer", layer->GetDisplayName(),
            "sublayer", dependencies, stage, layer, resolver, seenReferences);
    }

    // iterate all prims in this layer
    TraverseLayer(layer, [&](const SdfPrimSpecHandle& primSpec) {
        const std::string primPath = primSpec->GetPath().GetString();

        // collect dependencies from attributes
        for (const SdfAttributeSpecHandle& attr : primSpec->GetAttributes())
        {
            if (!attr)
                continue;

            const std::string attrName = attr->GetName();

            // asset type attribute
            if (attr->GetTypeName() == SdfValueTypeNames->Asset)
            {
                // collect attribute value
                VtValue defaultVal = attr->GetDefaultValue();
                if (defaultVal.IsHolding<SdfAssetPath>())
                {
                    SdfAssetPath val = defaultVal.UncheckedGet<SdfAssetPath>();
                    AddDependency(val.GetAssetPath(), "attr", primPath, attrName,
                        dependencies, stage, layer, resolver, seenReferences);
                }

                // collect time samples
                std::set<double> timeSamples = layer->ListTimeSamplesForPath(attr->GetPath());
                for (double t : timeSamples)
                {
                    SdfAssetPath val_t;
                    if (layer->QueryTimeSample(attr->GetPath(), t, &val_t))
                    {
                        AddDependency(val_t.GetAssetPath(), "attr", primPath, attrName,
                            dependencies, stage, layer, resolver, seenReferences);
                    }
                }
            }

            // asset array type attribute
            else if (attr->GetTypeName() == SdfValueTypeNames->AssetArray)
            {
                // collect attribute value
                VtValue defaultVal = attr->GetDefaultValue();
                if (defaultVal.IsHolding<VtArray<SdfAssetPath>>())
                {
                    VtArray<SdfAssetPath> arr = defaultVal.UncheckedGet<VtArray<SdfAssetPath>>();

                    for (SdfAssetPath val : arr)
                    {
                        AddDependency(val.GetAssetPath(), "attr", primPath, attrName,
                            dependencies, stage, layer, resolver, seenReferences);
                    }
                }

                // collect time samples
                std::set<double> timeSamples = layer->ListTimeSamplesForPath(attr->GetPath());
                for (double t : timeSamples)
                {
                    VtArray<SdfAssetPath> arr_t;
                    if (layer->QueryTimeSample(attr->GetPath(), t, &arr_t))
                    {
                        for (SdfAssetPath val : arr_t)
                        {
                            AddDependency(val.GetAssetPath(), "attr", primPath, attrName,
                                dependencies, stage, layer, resolver, seenReferences);
                        }
                    }
                }
            }
        }
        
        // collect references
        const auto& refList = primSpec->GetReferenceList();
        SdfReferenceVector refs;
        {
            // combine all authored list-op opinions
            refs.insert(refs.end(),
                refList.GetPrependedItems().begin(),
                refList.GetPrependedItems().end());
            refs.insert(refs.end(),
                refList.GetAppendedItems().begin(),
                refList.GetAppendedItems().end());
            refs.insert(refs.end(),
                refList.GetAddedItems().begin(),
                refList.GetAddedItems().end());
            refs.insert(refs.end(),
                refList.GetExplicitItems().begin(),
                refList.GetExplicitItems().end());
        }
        for (const SdfReference& ref : refs)
        {
            AddDependency(ref.GetAssetPath(), "reference", primPath, "reference",
                dependencies, stage, layer, resolver, seenReferences);
        }

        // collect payloads
        const auto& payloadList = primSpec->GetPayloadList();
        SdfPayloadVector payloads;
        {
            // combine all authored list-op opinions
            payloads.insert(payloads.end(),
                payloadList.GetPrependedItems().begin(),
                payloadList.GetPrependedItems().end());
            payloads.insert(payloads.end(),
                payloadList.GetAppendedItems().begin(),
                payloadList.GetAppendedItems().end());
            payloads.insert(payloads.end(),
                payloadList.GetAddedItems().begin(),
                payloadList.GetAddedItems().end());
            payloads.insert(payloads.end(),
                payloadList.GetExplicitItems().begin(),
                payloadList.GetExplicitItems().end());
        }
        for (const SdfPayload& p : payloads)
        {
            AddDependency(p.GetAssetPath(), "payload", primPath, "payload",
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
    std::unordered_set<std::string> seenReferences;

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
 * Helper function to append value to a search path string.
 */
inline void AppendArnoldSearchPath(AtNode* options, const AtString& param, const std::string value)
{
    if (value.empty())
        return;

    AtString currentSearchPath = AiNodeGetStr(options, param);
    std::string newSearchPath;
    if (!currentSearchPath.empty())
        newSearchPath = std::string(currentSearchPath.c_str()) + ";" + value;
    else
        newSearchPath = value;

    AiNodeSetStr(options, param, AtString(newSearchPath.c_str()));
}

/**
 * Translates Arnold search paths defined in a USD scene
 * to an Arnold scene.
 */
inline void TranslateSearchPaths(UsdStageRefPtr stage, AtUniverse* universe)
{
    if (universe == nullptr)
        return;

    AtNode* options = AiUniverseGetOptions(universe);
    if (options == nullptr)
        return;

    AtString asset_searchpath_str("asset_searchpath");
    AtString texture_searchpath_str("texture_searchpath");
    AtString procedural_searchpath_str("procedural_searchpath");

    for (const UsdPrim& prim : stage->Traverse())
    {
        if (prim.IsA<UsdRenderSettings>())
        {
            // asset search path
            UsdAttribute assetSearchPathAttr = prim.GetAttribute(TfToken("arnold:asset_searchpath"));
            std::string assetSearchPath;
            if (assetSearchPathAttr && assetSearchPathAttr.Get(&assetSearchPath))
                AppendArnoldSearchPath(options, asset_searchpath_str, assetSearchPath);
            // texture search path
            UsdAttribute textureSearchPathAttr = prim.GetAttribute(TfToken("arnold:texture_searchpath"));
            std::string textureSearchPath;
            if (textureSearchPathAttr && textureSearchPathAttr.Get(&textureSearchPath))
                AppendArnoldSearchPath(options, texture_searchpath_str, textureSearchPath);
            // procedural search path
            UsdAttribute proceduralSearchPathAttr = prim.GetAttribute(TfToken("arnold:procedural_searchpath"));
            std::string proceduralSearchPath;
            if (proceduralSearchPathAttr && proceduralSearchPathAttr.Get(&proceduralSearchPath))
                AppendArnoldSearchPath(options, procedural_searchpath_str, proceduralSearchPath);
        }
    }
}

/**
 * A helper to resolve paths defined in a USD scene with Arnold.
 *
 * The USD scene can define paths for Arnold nodes, like textures
 * in image nodes. These are not resolved by USD, but translated directly 
 * to the Arnold scene and resolved by Arnold when rendering the scene.
 */
class ArnoldPathResolver
{
public:
    ArnoldPathResolver() = default;

    void Initialize(UsdStageRefPtr stage)
    {
        if (!stage)
            return;

        if (!AiArnoldIsActive())
        {
            AiBegin();
            m_ownArnoldSession = true;
        }

        m_universe = AiUniverse();
        if (m_universe == nullptr)
        {
            AiMsgError("[usd] Failed to create Arnold universe");
            return;
        }
    
        // find search paths in the USD scene
        TranslateSearchPaths(stage, m_universe);
    }

    ~ArnoldPathResolver()
    {
        if (m_universe)
            AiUniverseDestroy(m_universe);
        if (m_ownArnoldSession)
            AiEnd();
    }

    std::string resolve(const std::string& path, AtFileType fileType)
    {
        if (path.empty())
            return std::string();

        const AtString& resolvedPathStr = AiResolveFilePath(path.c_str(), fileType);
        if (!resolvedPathStr.empty())
            return resolvedPathStr.c_str();
        return std::string();
    }

private:
    bool m_ownArnoldSession = false;
    AtUniverse* m_universe = nullptr;
};

inline AtString StdToAtString(const std::string& str)
{
    return !str.empty() ? AtString(str.c_str()) : AtString();
}

/**
 * Returns all assets found in the given USD scene.
 * 
 * The function collects all dependencies 
 * and converts them to Arnold assets.
 * 
 * It can be called from a scene format plugin or 
 * from a procedural plugin, shown by the isProcedural
 * argument. Resolving Arnold paths is different based on
 * where the function is used,
 */
bool CollectSceneAssets(const std::string& filename, std::vector<AtAsset*>& assets, bool isProcedural)
{
    // open the scene file
    UsdStageRefPtr stage = UsdStage::Open(filename);

    if (!stage)
    {
        AiMsgError("Failed to open stage: %s", filename.c_str());
        return false;
    }

    // create an Arnold path resolver
    // The USD scene can define paths for Arnold nodes, like textures
    // in image nodes. These are not resolved by USD, but translated directly 
    // to the Arnold scene and resolved by Arnold when rendering the scene.
    // We need to resolve these paths via the Arnold API.
    ArnoldPathResolver arnoldPathResolver;
    if (!isProcedural)
        arnoldPathResolver.Initialize(stage);

    // collect dependencies from the USD scene
    std::vector<USDDependency> dependencies = CollectDependencies(stage);

    // manage unresolved dependencies
    // these are potentally paths that can be resolved wih Arnold
    for (USDDependency& dep : dependencies)
    {
        if (dep.authoredPath.empty())
            continue;

        // resolve the path with Arnold
        if (dep.resolvedPath.empty())
            dep.resolvedPath = arnoldPathResolver.resolve(dep.authoredPath, dep.GetArnoldFileType());
    }

    // log dependencies
    for (const USDDependency& dep : dependencies)
    {
        std::string src = dep.primPath;
        if (dep.type == "attr" && !dep.attribute.empty())
            src += "/" + dep.attribute;
        AiMsgDebug("[usd] scene dependency: %s (ref: %s, type: %s, src: %s, layer: %s)",
            !dep.resolvedPath.empty() ? dep.resolvedPath.c_str() : dep.authoredPath.c_str(),
            dep.authoredPath.c_str(), dep.type.c_str(), !src.empty() ? src.c_str() : "", 
            dep.layer.c_str());
    }

    // convert dependencies to assets
    for (const USDDependency& dep : dependencies)
    {
        if (dep.authoredPath.empty())
            continue;

        std::string resolvedPath = dep.resolvedPath;
        // if could not resolve the path, then use the scene reference
        if (resolvedPath.empty())
            resolvedPath = dep.authoredPath;

        // add the dependency to the asset list
        assets.push_back(AiAsset(resolvedPath.c_str(), dep.GetArnoldFileType(), StdToAtString(dep.authoredPath),
            StdToAtString(dep.primPath), StdToAtString(dep.attribute)));
    }

    return true;
}

/**
 * Collects dependencies using standard USD API.
 * This function returns only resolve paths, but can not tell
 * how and where these files are references in the scene.
 */
void ComputeAllDependencies(const std::string& filename, std::vector<std::string>& dependencies)
{
    // collect all asset dependencies (layers, references, payloads, unresolved) for a USD file
    // prepare containers for outputs
    std::vector<SdfLayerRefPtr> layerDeps;
    std::vector<std::string> assetDeps;
    std::vector<std::string> unresolvedDeps;

    // create an SdfAssetPath for the input
    SdfAssetPath rootAssetPath(filename);

    // read dependencies
    bool ok = UsdUtilsComputeAllDependencies(
        rootAssetPath,
        &layerDeps,
        &assetDeps,
        &unresolvedDeps);

    if (!ok)
    {
        AiMsgError("Could not resolve dependencies for %s", filename);
        return;
    }

    // layer dependencies (opened as layers)
    for (auto const &layerPtr : layerDeps)
    {
        const std::string& layerPath = layerPtr->GetIdentifier();
        dependencies.push_back(layerPath);
        AiMsgDebug("[usd] scene dependency: %s (type: layer)", layerPath.c_str());
    }
    // other asset dependencies (references, payloads, clips, etc)
    for (const std::string& path : assetDeps)
    {
        dependencies.push_back(path);
        AiMsgDebug("[usd] scene dependency: %s (type: asset)", path.c_str());
    }
    // unresolved asset paths (could not resolve)
    for (const std::string& unresolvedPath : unresolvedDeps)
    {
        dependencies.push_back(unresolvedPath);
        AiMsgWarning("[usd] unresolved scene dependency: %s", unresolvedPath.c_str());
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

