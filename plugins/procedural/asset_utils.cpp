//
// SPDX-License-Identifier: Apache-2.0
//

#include "asset_utils.h"

#define USE_COMPUTE_ALL_DEPENDENCIES 0

// Asset API was added in Arnold 7.4.5.0
#if ARNOLD_VERSION_NUM >= 70405

#include <pxr/pxr.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/ar/defaultResolverContext.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/variantSpec.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primFlags.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdUtils/dependencies.h>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

typedef std::unordered_map<std::string, std::pair<std::string, std::string>> SeenReferenceMap;

struct SdfPrimSpecHandleHash
{
    size_t operator()(const SdfPrimSpecHandle& h) const noexcept
    {
        return hash_value(h);
    }
};

struct SdfPrimSpecHandleEqual
{
    bool operator()(const SdfPrimSpecHandle& a,
                    const SdfPrimSpecHandle& b) const noexcept
    {
        return a == b;
    }
};

inline bool _IsFileRelative(const std::string& path) { return path.find("./") == 0 || path.find("../") == 0; }
inline bool _IsRelativePath(const std::string& path) { return (!path.empty() && TfIsRelativePath(path)); }
inline bool _IsSearchPath(const std::string& path) { return _IsRelativePath(path) && !_IsFileRelative(path); }
inline std::string _AnchorRelativePath(const std::string& anchorLayerPath, const std::string& relativePath)
{
    const std::string anchorPath = TfGetPathName(anchorLayerPath);
    return anchorPath.empty() ? relativePath : TfStringCatPaths(anchorPath, relativePath);
}

typedef std::unordered_map<const SdfPrimSpecHandle, std::vector<UsdPrim>, SdfPrimSpecHandleHash, SdfPrimSpecHandleEqual> UsdPrimMap;

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
 * Helper struct that passed to dependency collector functions.
 */
struct DependencyData
{
    UsdStageRefPtr stage;
    SdfLayerHandle layer;
    std::vector<USDDependency> dependencies;
    SeenReferenceMap seenReferences;
    ArResolver& resolver = ArGetResolver();
    UsdPrimMap usdPrimMap;
};

void TraversePrimSpecs(const SdfPrimSpecHandle& prim, DependencyData& data);

bool GetAttributeCustomDataBool(const SdfLayerHandle& layer, const SdfPath& attrPath, 
    const TfToken& key, bool defaultValue)
{
    if (!layer || !attrPath.IsPropertyPath())
        return defaultValue;

    SdfSpecHandle spec = layer->GetObjectAtPath(attrPath);
    SdfAttributeSpecHandle attr = TfDynamic_cast<SdfAttributeSpecHandle>(spec);
    if (!attr)
        return defaultValue;

    VtDictionary dict = attr->GetCustomData();

    auto it = dict.find(key);
    if (it == dict.end())
        return defaultValue;

    VtValue& v = it->second;

    // defined as bool 
    if (v.IsHolding<bool>())
        return v.UncheckedGet<bool>();

    // defined as int
    if (v.IsHolding<int>())
        return v.UncheckedGet<int>() != 0;

    return defaultValue;
}

/**
 * Adds the given dependency to our list.
 */
inline void AddDependency(const std::string& ref, USDDependency::Type type,
    const SdfPath& primPath, const TfToken& primTypeName, const SdfPath& attribute,
    DependencyData& data)
{
    if (ref.empty())
        return;

    std::string anchoredPath;
    std::string resolvedPath;

    // the reference was already processed, use the resolved paths
    std::string layerName = data.layer ? data.layer->GetIdentifier() : std::string();
    std::string refId = layerName + "#" + ref;
    if (data.seenReferences.find(refId) != data.seenReferences.end())
    {
        auto refPaths = data.seenReferences[refId];
        anchoredPath = refPaths.first;
        resolvedPath = refPaths.second;
    }
    else
    {
        // resolve the reference to an absolute path
        std::string refPath = SdfComputeAssetPathRelativeToLayer(data.layer, ref);
        resolvedPath = data.resolver.Resolve(refPath);
        // If USD can not resolve the path this could be an Arnold specific path, like UDIM.
        // It can also be a relative search path, in that case the resolver doesn't return the absolute path
        // If the asset comes from a prim attribute, check if the "arnold_relative_path" metadata 
        // is defined on an attribute, which means Arnold needs to resolve the relative path.
        // If not defined, then use the absolute path returned by SdfComputeAssetPathRelativeToLayer.
        if (resolvedPath.empty() && TfIsRelativePath(ref))
        {
            bool remap = true;
            if (type == USDDependency::Type::Attribute)
            {
                bool isArnoldRelativePath = GetAttributeCustomDataBool(data.layer, attribute, TfToken("arnold_relative_path"), false);
                remap = !isArnoldRelativePath;
            }

            if (remap)
                resolvedPath = refPath;
        }
        anchoredPath = ref;
        // convert a relative reference relative to the main scene
        if (!resolvedPath.empty() && TfIsRelativePath(resolvedPath))
        {
            if (ref.find("UDIM") != std::string::npos) {
                std::cout << "resolved path not empty but ref is relative " << resolvedPath << std::endl;
            }

            // If the resolved path is still relative, it could be a search path with UDIM
            if (_IsSearchPath(resolvedPath) && resolvedPath.find("<UDIM>") != std::string::npos) {
                // Search all configured search paths for matching UDIM files.
                // Mirrors ArDefaultResolver::_Resolve: layer dir first, then bound context paths.
                // We probe with tile 1001, which is always the first tile in a UDIM sequence.
                const std::string udimProbe = TfStringReplace(resolvedPath, "<UDIM>", "1001");

                std::vector<std::string> searchDirs;
                // 1. Layer directory (mirrors file-relative anchoring priority)
                searchDirs.push_back(TfGetPathName(data.layer->GetIdentifier()));
                // 2. Directories from the currently-bound ArDefaultResolverContext
                const ArResolverContext ctx = data.resolver.GetCurrentContext();
                if (const ArDefaultResolverContext* defCtx = ctx.Get<ArDefaultResolverContext>()) {
                    for (const std::string& sp : defCtx->GetSearchPath())
                        searchDirs.push_back(sp);
                }
                std::string foundDir;
                for (const std::string& dir : searchDirs) {
                    if (dir.empty())
                        continue;
                    if (TfPathExists(TfStringCatPaths(dir, udimProbe))) {
                        foundDir = dir;
                        break;
                    }
                }
                if (!foundDir.empty()) {
                    anchoredPath = "./" + resolvedPath;
                    resolvedPath = TfStringCatPaths(foundDir, resolvedPath);
                }
            } else {
                std::string relativeToRoot = ComputeRelativePathToRoot(data.stage, resolvedPath);
                // convert only if the file is located under the root folder
                if (!relativeToRoot.empty() && relativeToRoot.at(0) != '.')
                    anchoredPath = relativeToRoot;
            }
        }
        data.seenReferences[refId] = std::make_pair(anchoredPath, resolvedPath);
    }

    // create a dependency
    data.dependencies.push_back(USDDependency(type, anchoredPath,
        resolvedPath, data.layer, primPath, primTypeName, attribute));
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
inline void CollectOslShaderDependencies(const SdfPrimSpecHandle& prim, DependencyData& data)
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

            CollectAttrDependencies<std::string>(pathAttr, data.layer,
                [&](const std::string& val) {
                    AddDependency(val, USDDependency::Type::Attribute, 
                        prim->GetPath(), prim->GetTypeName(), pathAttr->GetPath(), data);
                });
        }
    }
    AiParamIteratorDestroy(piter);

    // cleanup
    AiUniverseDestroy(universe);
}

/**
 * Returns all Usd prims that include the given Sdf prim spec.
 */
inline std::vector<UsdPrim> FindUsdPrims(const SdfPrimSpecHandle& primSpec, UsdPrimMap& usdPrimMap)
{
    auto it = usdPrimMap.find(primSpec);
    return it != usdPrimMap.end() ? it->second : std::vector<UsdPrim>();
}

/**
 * Builds an (Sdf prim - Usd prim list) map.
 * It lists all Usd prims that an Sdf prim is contributing to.
 */
inline void CreateUsdPrimMap(const UsdStageRefPtr& stage, UsdPrimMap& usdPrimMap)
{
    // Use UsdPrimAllPrimsPredicate so that 'over' prims are included.
    // When CollectSceneAssets is called directly on a file whose prims are all
    // 'over' specifiers (e.g. a stitched clip file), the default predicate
    // (UsdPrimDefaultPredicate, which requires UsdPrimIsDefined) would return
    // nothing and leave the map empty, causing variant traversal to be skipped.
    for (const UsdPrim& usdPrim : UsdPrimRange::Stage(stage, UsdPrimAllPrimsPredicate))
    {
        if (!usdPrim)
            continue;

        for (const SdfPrimSpecHandle& spec : usdPrim.GetPrimStack())
        {
            if (!spec)
                continue;

            usdPrimMap[spec].push_back(usdPrim);
        }
    }
}

/**
 * Returns dependencies found in the selected variants of a prim.
 */
inline void CollectDependenciesFromVariants(const SdfPrimSpecHandle& prim, DependencyData& data)
{
    // skip when no variant sets are defined in the prim
    const auto variantSets = prim->GetVariantSets();
    if (variantSets.empty())
        return;

    // we need to read variant selections from Usd prims
    // the Sdf prim contains selections defined within the layer that authors the prim,
    // while the Usd prim contains composed selections across all layers
    // a prim spec can contribute to multiple Usd prims
    std::vector<UsdPrim> usdPrims = FindUsdPrims(prim, data.usdPrimMap);

    // interate the variant sets
    for (const auto& vsetit : variantSets.items())
    {
        const std::string setName = vsetit.first;
        const SdfVariantSetSpecHandle& vset = vsetit.second;
        if (!vset)
            continue;

        // Get the selected variants from composed Usd prims (respects stronger-layer
        // overrides). When the selection cannot be determined — e.g. the stage was
        // opened directly from a file whose prims are all 'over' specifiers, or no
        // variant is selected — fall back to traversing ALL variants so that clip
        // dependencies (manifest, assetPaths, etc.) are never silently missed.
        // This matches the behaviour of UsdUtils_LocalizationContext::_ProcessLayer
        // which unconditionally traverses every variant spec.
        std::unordered_set<std::string> selectedVariants;
        for (const UsdPrim usdPrim : usdPrims)
        {
            UsdVariantSet usdVset = usdPrim.GetVariantSet(setName);
            if (!usdVset)
                continue;
            std::string selectedVariantName = usdVset.GetVariantSelection();
            if (!selectedVariantName.empty())
                selectedVariants.insert(selectedVariantName);
        }
        const bool traverseAllVariants = selectedVariants.empty();

        // iterate all variants in the set
        for (const SdfVariantSpecHandle& variant : vset->GetVariants())
        {
            if (!variant)
                continue;

            // skip unselected variants only when a selection is known
            const std::string variantName = variant->GetName();
            if (!traverseAllVariants && !selectedVariants.count(variantName))
                continue;

            // get the root prim spec for the variant
            const SdfPrimSpecHandle vPrim = variant->GetPrimSpec();
            if (!vPrim)
                continue;

            // scan the variant-authored prim spec
            TraversePrimSpecs(vPrim, data);
        }
    }
}

/**
 * Stores asset paths from the given clip dictionary value.
 */
inline void CollectClipDependencies(const VtValue& value, const SdfPrimSpecHandle& prim, DependencyData& data)
{
    if (value.IsHolding<SdfAssetPath>())
    {
        SdfAssetPath v = value.UncheckedGet<SdfAssetPath>();
        AddDependency(v.GetAssetPath(), USDDependency::Type::Clip,
            prim->GetPath(), prim->GetTypeName(), SdfPath(), data);
    }
    else if (value.IsHolding<VtArray<SdfAssetPath>>())
    {
        const auto& arr = value.UncheckedGet<VtArray<SdfAssetPath>>();
        for (SdfAssetPath v : arr)
        {
            AddDependency(v.GetAssetPath(), USDDependency::Type::Clip,
                prim->GetPath(), prim->GetTypeName(), SdfPath(), data);
        }
    }
    else if (value.IsHolding<std::vector<SdfAssetPath>>())
    {
        const auto& arr = value.UncheckedGet<std::vector<SdfAssetPath>>();
        for (SdfAssetPath v : arr)
        {
            AddDependency(v.GetAssetPath(), USDDependency::Type::Clip,
                prim->GetPath(), prim->GetTypeName(), SdfPath(), data);
        }
    }
    else if (value.IsHolding<VtDictionary>())
    {
        const VtDictionary& dict = value.UncheckedGet<VtDictionary>();
        for (const auto& it : dict)
            CollectClipDependencies(it.second, prim, data);
    }
}

/**
 * Stores dependencies found in the clips metadata on a prim.
 * Value clips are a mechanism for providing time-varying data from external USD files.
 */
inline void CollectDependenciesFromClips(const SdfPrimSpecHandle& prim, DependencyData& data)
{
    if (!prim)
        return;

    static const TfToken clipsToken("clips");
    const VtValue clipsValue = prim->GetInfo(clipsToken);

    if (!clipsValue.IsHolding<VtDictionary>())
        return;

    const VtDictionary& clipsDict = clipsValue.UncheckedGet<VtDictionary>();

    // clips = {
    //   dictionary <clipSetName> = {
    //       ...
    //       asset manifestAssetPath = ...
    //       asset[] assetPaths = [...]
    //   }
    // }
    for (const auto& clipSetIt : clipsDict)
    {
        const std::string& clipSetName = clipSetIt.first;
        const VtValue& clipSetValue = clipSetIt.second;

        if (!clipSetValue.IsHolding<VtDictionary>())
            continue;

        const VtDictionary& clipSetDict = clipSetValue.UncheckedGet<VtDictionary>();

        for (const auto& entry : clipSetDict)
        {
            const VtValue& value = entry.second;
            CollectClipDependencies(value, prim, data);
        }

        // Handle templateAssetPath: a string like "clips/shot.###.usd" where '#'
        // characters form a pattern. USD expands this by globbing the filesystem.
        // This is separate from assetPaths (explicit SdfAssetPath list) and is
        // completely missed by CollectClipDependencies which only handles SdfAssetPath.
        static const std::string templateKey("templateAssetPath");
        const VtValue* templateValue = clipSetDict.GetValueAtPath(templateKey);
        if (templateValue && templateValue->IsHolding<std::string>() && data.layer)
        {
            const std::string& templatePath = templateValue->UncheckedGet<std::string>();
            const std::string clipsDir = TfGetPathName(templatePath);
            if (!clipsDir.empty())
            {
                const std::string clipsDirResolved =
                    SdfComputeAssetPathRelativeToLayer(data.layer, clipsDir);

                if (TfIsDir(clipsDirResolved))
                {
                    const std::string baseName = TfGetBaseName(templatePath);
                    const std::string globPattern = TfStringCatPaths(
                        clipsDirResolved, TfStringReplace(baseName, "#", "*"));

                    std::vector<std::string> clipFiles = TfGlob(globPattern);
                    // TfGlob returns the pattern unchanged when there are no matches
                    if (clipFiles.size() == 1 && clipFiles[0] == globPattern)
                        clipFiles.clear();

                    for (const std::string& clipFile : clipFiles)
                    {
                        // Reconstruct path relative to the layer (matching USD behaviour)
                        const std::string relClipFile =
                            TfStringReplace(clipFile, clipsDirResolved + '/', clipsDir);
                        AddDependency(relClipFile, USDDependency::Type::Clip,
                            prim->GetPath(), prim->GetTypeName(), SdfPath(), data);
                    }
                }
            }
        }
    }
}

/**
 * Returns dependencies found in a prim. This includes dependencies
 * defined in asset type attributes, references and payloads.
 */
inline void CollectPrimDependencies(const SdfPrimSpecHandle& prim, DependencyData& data)
{
    // collect dependencies from attributes
    for (const SdfAttributeSpecHandle& attr : prim->GetAttributes())
    {
        if (!attr)
            continue;

        const std::string attrName = attr->GetName();

        // asset type attribute
        if (attr->GetTypeName() == SdfValueTypeNames->Asset)
        {
            CollectAttrDependencies<SdfAssetPath>(attr, data.layer,
                [&](const SdfAssetPath& val) {
                    AddDependency(val.GetAssetPath(), USDDependency::Type::Attribute, 
                        prim->GetPath(), prim->GetTypeName(), attr->GetPath(), data);
                });
        }

        // asset array type attribute
        else if (attr->GetTypeName() == SdfValueTypeNames->AssetArray)
        {
            CollectAttrDependencies<VtArray<SdfAssetPath>>(attr, data.layer,
                [&](const VtArray<SdfAssetPath>& arr) {
                    for (SdfAssetPath val : arr)
                    {
                        AddDependency(val.GetAssetPath(), USDDependency::Type::Attribute,
                            prim->GetPath(), prim->GetTypeName(), attr->GetPath(), data);
                    }
                });
        }

        // NOTE filename in ArnoldUsd is a string type not an asset type
        // therefore it needs special care
        if (attrName == "arnold:filename" && prim->GetTypeName().GetString() == "ArnoldUsd")
        {
            CollectAttrDependencies<std::string>(attr, data.layer,
                [&](const std::string& val) {
                    AddDependency(val, USDDependency::Type::Attribute, 
                        prim->GetPath(), prim->GetTypeName(), attr->GetPath(), data);
                });
        }
    }

    // collect dependencies from Arnold OSL shader
    if (IsArnoldShader(prim, TfToken("osl")))
        CollectOslShaderDependencies(prim, data);
        
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
            prim->GetPath(), prim->GetTypeName(), SdfPath(), data);
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
            prim->GetPath(), prim->GetTypeName(), SdfPath(), data);
    }

    // collect dependencies from value clips
    CollectDependenciesFromClips(prim, data);

    // collect dependencies from variants
    CollectDependenciesFromVariants(prim, data);
}

/**
 * Helper function to iterate over all prims in a layer.
 */
inline void TraversePrimSpecs(const SdfPrimSpecHandle& prim, DependencyData& data)
{
    // collect dependencies from the prim
    CollectPrimDependencies(prim, data);

    // iterate descendants
    for (const auto& child : prim->GetNameChildren())
        TraversePrimSpecs(child, data);
}

/**
 * Helper function to iterate over all prims in a layer.
 */
inline void TraverseLayer(const SdfLayerHandle& layer, DependencyData& data)
{
    SdfPrimSpecHandle root = layer->GetPseudoRoot();

    for (const auto& prim : root->GetNameChildren())
        TraversePrimSpecs(prim, data);
}

/**
 * Returns all dependencies found in the given layer.
 */
inline void CollectDependenciesFromLayer(const SdfLayerHandle& layer, DependencyData& data)
{
    if (!layer)
        return;

    data.layer = layer;

    // collect sublayers
    for (const std::string& sub : layer->GetSubLayerPaths())
    {
        AddDependency(sub, USDDependency::Type::Sublayer, SdfPath(), TfToken(), SdfPath(), data);
    }

    // iterate all prims in this layer
    TraverseLayer(layer, data);
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
    DependencyData data;
    data.stage = stage;

    // create a map that lists all UsdPrims that include an SdfPrim
    CreateUsdPrimMap(stage, data.usdPrimMap);

    // Track layers that have already been traversed to avoid duplicate work.
    // Clip files are demand-loaded by USD and are NOT in GetUsedLayers() until
    // attribute values at specific times are accessed. Mirror what
    // UsdUtils_LocalizationContext does: open every discovered USD file as a
    // layer and traverse it, processing transitive dependencies as a queue.
    std::unordered_set<std::string> processedLayerIds;

    // collect dependencies from all used layers
    SdfLayerHandleVector usedLayers = stage->GetUsedLayers();
    for (auto &layer : usedLayers)
    {
        processedLayerIds.insert(layer->GetIdentifier());
        CollectDependenciesFromLayer(layer, data);
    }

    // Follow any USD-file dependencies that were discovered in clip metadata
    // (manifestAssetPath, assetPaths, templateAssetPath expansions) but were
    // not yet opened as layers. New entries may appear as inner layers are
    // traversed, so iterate until the list stabilises.
    for (size_t i = 0; i < data.dependencies.size(); ++i)
    {
        const std::string& resolvedPath = data.dependencies[i].resolvedPath;
        if (resolvedPath.empty() || !UsdStage::IsSupportedFile(resolvedPath))
            continue;
        if (processedLayerIds.count(resolvedPath))
            continue;

        processedLayerIds.insert(resolvedPath);

        SdfLayerRefPtr depLayer = SdfLayer::FindOrOpen(resolvedPath);
        if (!depLayer)
            continue;

        // a layer can have a different identifier from its resolved path
        if (!processedLayerIds.insert(depLayer->GetIdentifier()).second)
            continue;

        CollectDependenciesFromLayer(depLayer, data);
    }

    return data.dependencies;
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
        case USDDependency::Type::Clip:
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
        case USDDependency::Type::Clip: return "clips";
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

// Use UsdUtilsComputeAllDependencies and compare the dependencies found by USD
// This might return more dependencies than needed
#if USE_COMPUTE_ALL_DEPENDENCIES
/**
 * Collects dependencies using standard USD API.
 *
 * This function returns only resolved paths, but can not tell
 * how and where these files are referenced in the scene.
 */
inline void ComputeAllDependencies(UsdStageRefPtr& stage, std::vector<std::string>& dependencies)
{
    // collect all asset dependencies (layers, references, payloads, unresolved) for a USD file
    // prepare containers for outputs
    std::vector<SdfLayerRefPtr> layerDeps;
    std::vector<std::string> assetDeps;
    std::vector<std::string> unresolvedDeps;

    // read dependencies
    std::string rootLayerPath = stage->GetRootLayer()->GetIdentifier();
    SdfAssetPath rootLayerAssetPath(rootLayerPath);
    bool ok = UsdUtilsComputeAllDependencies(
        rootLayerAssetPath,
        &layerDeps,
        &assetDeps,
        &unresolvedDeps);

    if (!ok)
    {
        AiMsgWarning("Could not resolve dependencies via UsdUtilsComputeAllDependencies");
        return;
    }

    // layer dependencies (opened as layers)
    for (auto const &layerPtr : layerDeps)
    {
        const std::string& layerPath = layerPtr->GetIdentifier();
        // ignore the root layer
        if (layerPath != rootLayerPath)
            dependencies.push_back(layerPath);
    }
    // other asset dependencies (references, payloads, clips, etc)
    for (const std::string& path : assetDeps)
        dependencies.push_back(path);

    /* - we don't care about unresolved dependencies for now
    // unresolved asset paths (could not resolve)
    for (const std::string& unresolvedPath : unresolvedDeps)
        dependencies.push_back(unresolvedPath);
    */
}

inline std::string NormalizePath(const std::string path)
{
#ifdef WIN32
    std::string normalizedPath = path;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });
    return normalizedPath;
#else
    return path;
#endif
}

inline void AddMissedDependencies(UsdStageRefPtr& stage, const std::vector<USDDependency>& dependencies, std::vector<AtAsset*>& assets)
{
    std::unordered_set<std::string> foundDependencies;
    for (const USDDependency& dep : dependencies)
        foundDependencies.insert(NormalizePath(dep.resolvedPath));

    std::vector<std::string> stageDependencies;
    ComputeAllDependencies(stage, stageDependencies);

    int numAdditionalDependencies = 0;
    for (const std::string& resolvedPath : stageDependencies)
    {
        if (!foundDependencies.count(NormalizePath(resolvedPath)))
        {
            AiMsgDebug("[usd] additional dependency: %s", resolvedPath.c_str());

            // add the dependency to the asset list
            AtAsset* asset = AiAsset(resolvedPath.c_str(), AtFileType::Custom);
            assets.push_back(asset);

            ++numAdditionalDependencies;
        }
    }

    if (numAdditionalDependencies)
        AiMsgDebug("[usd] %d additional dependencies found", numAdditionalDependencies);
}
#endif // USE_COMPUTE_ALL_DEPENDENCIES

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
#if USE_COMPUTE_ALL_DEPENDENCIES
    // look for missed dependencies
    if (!isProcedural)
    {
        AddMissedDependencies(stage, dependencies, assets);
    }
#endif
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // Asset API

