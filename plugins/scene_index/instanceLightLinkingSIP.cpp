#include "instanceLightLinkingSIP.h"

#ifdef ENABLE_SCENE_INDEX

#include <pxr/imaging/hd/version.h>

#if HD_API_VERSION >= 58
#include <pxr/imaging/hdsi/version.h>
#endif

#ifdef HdArnoldUSE_INSTANCE_LIGHT_LINKING_SPLIT

#include <pxr/imaging/hd/categoriesSchema.h>
#include <pxr/imaging/hd/instanceCategoriesSchema.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/instancerTopologySchema.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/base/tf/envSetting.h>
#include <iostream>
#include <algorithm>
#include <map>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdArnoldInstanceLightLinkingSplitSceneIndexPlugin")));

TF_DEFINE_ENV_SETTING(
    HdArnoldENABLE_INSTANCE_LIGHT_LINKING_SPLIT, true,
    "Split point-instancer prototypes by per-instance light-link categories.");

////////////////////////////////////////////////////////////////////////////////
// Plugin registration
////////////////////////////////////////////////////////////////////////////////

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HdArnoldInstanceLightLinkingSplitSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // Phase 5: after HdsiLightLinkingSceneIndex (phase 4) which populates
    // HdInstanceCategoriesSchema and HdCategoriesSchema.
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 5;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName,
        nullptr, insertionPhase, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

////////////////////////////////////////////////////////////////////////////////
// Scene Index Implementation
////////////////////////////////////////////////////////////////////////////////

/* static */
HdArnoldInstanceLightLinkingSplitSceneIndexRefPtr
HdArnoldInstanceLightLinkingSplitSceneIndex::New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    return TfCreateRefPtr(new HdArnoldInstanceLightLinkingSplitSceneIndex(inputSceneIndex));
}

HdArnoldInstanceLightLinkingSplitSceneIndex::HdArnoldInstanceLightLinkingSplitSceneIndex(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
#if PXR_VERSION >= 2308
    SetDisplayName("Arnold: per-instance light linking split");
#endif
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

/* static */
SdfPath HdArnoldInstanceLightLinkingSplitSceneIndex::_MakeSyntheticRootPath(
    const SdfPath &originalPath, size_t groupIndex)
{
    const std::string newName =
        originalPath.GetName() + "__arnLLGroup_" + std::to_string(groupIndex);
    return originalPath.GetParentPath().AppendChild(TfToken(newName));
}

/* static */
HdContainerDataSourceHandle HdArnoldInstanceLightLinkingSplitSceneIndex::_BuildCategoriesOverlay(
    const HdContainerDataSourceHandle &originalDS,
    const CategoryKey &categories)
{
    HdContainerDataSourceHandle catContainer = HdCategoriesSchema::BuildRetained(
        categories.size(),
        categories.empty() ? nullptr : categories.data(),
        0, nullptr);

    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdCategoriesSchema::GetSchemaToken(), catContainer),
        originalDS);
}

/* static */
HdContainerDataSourceHandle HdArnoldInstanceLightLinkingSplitSceneIndex::_BuildInstancedByOverlay(
    const HdContainerDataSourceHandle &originalDS,
    const SdfPath &newPrototypeRoot)
{
    HdInstancedBySchema origInstancedBy = HdInstancedBySchema::GetFromParent(originalDS);
    if (!origInstancedBy) {
        return originalDS;
    }

    // Keep the instancer paths, override the prototype roots.
    HdPathArrayDataSourceHandle pathsDS = origInstancedBy.GetPaths();
    if (!pathsDS) {
        return originalDS;
    }
    VtArray<SdfPath> instancerPaths = pathsDS->GetTypedValue(0.0f);
    if (instancerPaths.empty()) {
        return originalDS;
    }

    // Build prototypeRoots array: one entry per instancer, all pointing to synthetic root.
    VtArray<SdfPath> newRoots(instancerPaths.size(), newPrototypeRoot);

    HdContainerDataSourceHandle newInstancedBy = HdInstancedBySchema::BuildRetained(
        pathsDS,
        HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New(newRoots));

    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdInstancedBySchema::GetSchemaToken(), newInstancedBy),
        originalDS);
}

// ---------------------------------------------------------------------------
// Split state computation
// ---------------------------------------------------------------------------

bool HdArnoldInstanceLightLinkingSplitSceneIndex::_ComputeSplitState(
    const SdfPath &instancerPath,
    const HdSceneIndexPrim &prim,
    InstancerSplitState &outState) const
{
    if (!prim.dataSource) {
        return false;
    }

    HdInstanceCategoriesSchema instCatSchema =
        HdInstanceCategoriesSchema::GetFromParent(prim.dataSource);
    if (!instCatSchema) {
        return false;
    }
    HdVectorDataSourceHandle catValues = instCatSchema.GetCategoriesValues();
    if (!catValues || catValues->GetNumElements() == 0) {
        return false;
    }

    HdInstancerTopologySchema topoSchema =
        HdInstancerTopologySchema::GetFromParent(prim.dataSource);
    if (!topoSchema) {
        return false;
    }
    HdPathArrayDataSourceHandle protosDS = topoSchema.GetPrototypes();
    if (!protosDS) {
        return false;
    }
    VtArray<SdfPath> prototypes = protosDS->GetTypedValue(0.0f);
    if (prototypes.empty()) {
        return false;
    }

    HdIntArrayVectorSchema indicesVec = topoSchema.GetInstanceIndices();
    const size_t numCatSlots = catValues->GetNumElements();
    bool anySplit = false;

    for (size_t pi = 0; pi < prototypes.size(); ++pi) {
        const SdfPath &protoRoot = prototypes[pi];

        VtIntArray protoIndices;
        if (pi < indicesVec.GetNumElements()) {
            if (HdIntArrayDataSourceHandle ds = indicesVec.GetElement(pi)) {
                protoIndices = ds->GetTypedValue(0.0f);
            }
        }
        if (protoIndices.empty()) {
            continue;
        }

        // Group instance slots by their sorted category key.
        std::map<CategoryKey, VtIntArray> groups;
        for (int slot : protoIndices) {
            CategoryKey key;
            if (slot >= 0 && static_cast<size_t>(slot) < numCatSlots) {
                HdContainerDataSourceHandle catContainer =
                    HdContainerDataSource::Cast(catValues->GetElement(slot));
                if (catContainer) {
                    HdCategoriesSchema catSchema(catContainer);
                    VtArray<TfToken> names = catSchema.GetIncludedCategoryNames();
                    key.assign(names.begin(), names.end());
                    std::sort(key.begin(), key.end());
                }
            }
            groups[key].push_back(slot);
        }

        if (groups.size() <= 1) {
            continue;
        }

        anySplit = true;
        PrototypeSplit &split = outState.protoSplits[protoRoot];
        size_t groupIdx = 0;
        for (auto &[key, indices] : groups) {
            GroupInfo info;
            info.categories = key;
            info.indices = indices;
            info.protoRootPath =
                (groupIdx == 0) ? protoRoot : _MakeSyntheticRootPath(protoRoot, groupIdx);
            split.groups.push_back(std::move(info));
            ++groupIdx;
        }
    }

    return anySplit;
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void HdArnoldInstanceLightLinkingSplitSceneIndex::_CollectHierarchy(
    const SdfPath &rootPath, std::vector<std::pair<SdfPath, TfToken>> &outPrims) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(rootPath);
    outPrims.push_back({rootPath, prim.primType});
    for (const SdfPath &child : _GetInputSceneIndex()->GetChildPrimPaths(rootPath)) {
        _CollectHierarchy(child, outPrims);
    }
}

void HdArnoldInstanceLightLinkingSplitSceneIndex::_PopulateCache(const SdfPath &instancerPath)
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(instancerPath);
    InstancerSplitState state;
    if (!_ComputeSplitState(instancerPath, prim, state)) {
        return;
    }
    std::cout << "Populate cache" << std::endl;
    _instancerCache[instancerPath] = state;

    for (auto &[origRoot, split] : state.protoSplits) {
        if (split.groups.size() <= 1) {
            continue;
        }
        // Group 0 keeps the original prototype root path.
        _splitOriginalRoots[origRoot] = {instancerPath, split.groups[0].categories};

        // Groups 1+ get synthetic prototype root paths.
        for (size_t gi = 1; gi < split.groups.size(); ++gi) {
            const SdfPath &synRoot = split.groups[gi].protoRootPath;
            _syntheticRoots[synRoot] = {instancerPath, origRoot, split.groups[gi].categories};
            _parentToSyntheticRoots[synRoot.GetParentPath()].push_back(synRoot);
        }
    }
}

void HdArnoldInstanceLightLinkingSplitSceneIndex::_RemoveFromCache(const SdfPath &instancerPath)
{
    auto it = _instancerCache.find(instancerPath);
    if (it == _instancerCache.end()) {
        return;
    }

    for (auto &[origRoot, split] : it->second.protoSplits) {
        _splitOriginalRoots.erase(origRoot);

        for (size_t gi = 1; gi < split.groups.size(); ++gi) {
            const SdfPath &synRoot = split.groups[gi].protoRootPath;
            _syntheticRoots.erase(synRoot);

            auto parentIt = _parentToSyntheticRoots.find(synRoot.GetParentPath());
            if (parentIt != _parentToSyntheticRoots.end()) {
                auto &vec = parentIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), synRoot), vec.end());
                if (vec.empty()) {
                    _parentToSyntheticRoots.erase(parentIt);
                }
            }
        }
    }

    _instancerCache.erase(it);
}

// ---------------------------------------------------------------------------
// GetPrim
// ---------------------------------------------------------------------------

HdSceneIndexPrim HdArnoldInstanceLightLinkingSplitSceneIndex::_GetModifiedInstancerPrim(
    const SdfPath &path, const InstancerSplitState &state) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(path);
    if (!prim.dataSource) {
        return prim;
    }

    HdInstancerTopologySchema topoSchema =
        HdInstancerTopologySchema::GetFromParent(prim.dataSource);
    if (!topoSchema) {
        return prim;
    }

    HdPathArrayDataSourceHandle origProtosDS = topoSchema.GetPrototypes();
    VtArray<SdfPath> origPrototypes =
        origProtosDS ? origProtosDS->GetTypedValue(0.0f) : VtArray<SdfPath>();

    HdIntArrayVectorSchema origIndicesVec = topoSchema.GetInstanceIndices();

    VtArray<SdfPath> newPrototypes;
    std::vector<HdDataSourceBaseHandle> newIndexSources;

    for (size_t pi = 0; pi < origPrototypes.size(); ++pi) {
        const SdfPath &origRoot = origPrototypes[pi];
        auto splitIt = state.protoSplits.find(origRoot);

        if (splitIt == state.protoSplits.end() || splitIt->second.groups.size() <= 1) {
            newPrototypes.push_back(origRoot);
            if (pi < origIndicesVec.GetNumElements()) {
                newIndexSources.push_back(origIndicesVec.GetElement(pi));
            } else {
                newIndexSources.push_back(
                    HdRetainedTypedSampledDataSource<VtIntArray>::New(VtIntArray{}));
            }
        } else {
            for (const auto &group : splitIt->second.groups) {
                newPrototypes.push_back(group.protoRootPath);
                newIndexSources.push_back(
                    HdRetainedTypedSampledDataSource<VtIntArray>::New(group.indices));
            }
        }
    }

    HdContainerDataSourceHandle origTopologyDS = topoSchema.GetContainer();

    HdContainerDataSourceHandle modifiedTopology = HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdInstancerTopologySchemaTokens->prototypes,
            HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New(newPrototypes),
            HdInstancerTopologySchemaTokens->instanceIndices,
            HdRetainedSmallVectorDataSource::New(
                newIndexSources.size(), newIndexSources.data())),
        origTopologyDS);

    return {
        prim.primType,
        HdOverlayContainerDataSource::New(
            HdRetainedContainerDataSource::New(
                HdInstancerTopologySchema::GetSchemaToken(), modifiedTopology),
            prim.dataSource)};
}

HdSceneIndexPrim HdArnoldInstanceLightLinkingSplitSceneIndex::GetPrim(
    const SdfPath &primPath) const
{
    // Case 1: instancer prim that has a cached split.
    {
        auto it = _instancerCache.find(primPath);
        if (it != _instancerCache.end()) {
            return _GetModifiedInstancerPrim(primPath, it->second);
        }
    }

    // Case 2: synthetic prim (path under a synthetic prototype root).
    for (auto &[synRoot, entry] : _syntheticRoots) {
        if (primPath == synRoot || primPath.HasPrefix(synRoot)) {
            const SdfPath origPath = primPath.ReplacePrefix(synRoot, entry.originalRootPath);
            HdSceneIndexPrim origPrim = _GetInputSceneIndex()->GetPrim(origPath);
            if (!origPrim.dataSource) {
                return origPrim;
            }
            HdContainerDataSourceHandle ds =
                _BuildCategoriesOverlay(origPrim.dataSource, entry.groupCategories);
            ds = _BuildInstancedByOverlay(ds, synRoot);
            return {origPrim.primType, ds};
        }
    }

    // Case 3: prim under a split original prototype root — override with group-0 categories.
    for (auto &[origRoot, entry] : _splitOriginalRoots) {
        if (primPath == origRoot || primPath.HasPrefix(origRoot)) {
            HdSceneIndexPrim inputPrim = _GetInputSceneIndex()->GetPrim(primPath);
            if (!inputPrim.dataSource) {
                return inputPrim;
            }
            return {
                inputPrim.primType,
                _BuildCategoriesOverlay(inputPrim.dataSource, entry.group0Categories)};
        }
    }

    return _GetInputSceneIndex()->GetPrim(primPath);
}

// ---------------------------------------------------------------------------
// GetChildPrimPaths
// ---------------------------------------------------------------------------

SdfPathVector HdArnoldInstanceLightLinkingSplitSceneIndex::GetChildPrimPaths(
    const SdfPath &primPath) const
{
    // Check if primPath is a synthetic root or under one (remap to original subtree).
    for (auto &[synRoot, entry] : _syntheticRoots) {
        if (primPath == synRoot || primPath.HasPrefix(synRoot)) {
            const SdfPath origPath = primPath.ReplacePrefix(synRoot, entry.originalRootPath);
            SdfPathVector origChildren = _GetInputSceneIndex()->GetChildPrimPaths(origPath);
            SdfPathVector result;
            result.reserve(origChildren.size());
            for (const SdfPath &child : origChildren) {
                result.push_back(child.ReplacePrefix(entry.originalRootPath, synRoot));
            }
            return result;
        }
    }

    SdfPathVector children = _GetInputSceneIndex()->GetChildPrimPaths(primPath);

    // If primPath is the parent of synthetic roots, append them.
    auto it = _parentToSyntheticRoots.find(primPath);
    if (it != _parentToSyntheticRoots.end()) {
        children.insert(children.end(), it->second.begin(), it->second.end());
    }

    return children;
}

// ---------------------------------------------------------------------------
// Observer callbacks
// ---------------------------------------------------------------------------

void HdArnoldInstanceLightLinkingSplitSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    HdSceneIndexObserver::AddedPrimEntries extraAdded;

    for (const auto &entry : entries) {
        // Check whether this newly-added prim is an instancer we should split.
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(entry.primPath);
        if (!prim.dataSource) {
            continue;
        }
        HdInstanceCategoriesSchema instCatSchema =
            HdInstanceCategoriesSchema::GetFromParent(prim.dataSource);
        if (!instCatSchema) {
            continue;
        }
        HdInstancerTopologySchema topoSchema =
            HdInstancerTopologySchema::GetFromParent(prim.dataSource);
        if (!topoSchema) {
            continue;
        }

        _PopulateCache(entry.primPath);

        auto stateIt = _instancerCache.find(entry.primPath);
        if (stateIt == _instancerCache.end()) {
            continue;
        }

        // Emit PrimsAdded for each synthetic prototype hierarchy.
        for (auto &[origRoot, split] : stateIt->second.protoSplits) {
            if (split.groups.size() <= 1) {
                continue;
            }
            // Collect the original prototype hierarchy once.
            std::vector<std::pair<SdfPath, TfToken>> origHierarchy;
            _CollectHierarchy(origRoot, origHierarchy);

            for (size_t gi = 1; gi < split.groups.size(); ++gi) {
                const SdfPath &synRoot = split.groups[gi].protoRootPath;
                for (auto &[origPath, primType] : origHierarchy) {
                    const SdfPath synPath = origPath.ReplacePrefix(origRoot, synRoot);
                    extraAdded.push_back({synPath, primType});
                }
            }
        }
    }

    _SendPrimsAdded(entries);
    if (!extraAdded.empty()) {
        _SendPrimsAdded(extraAdded);
    }
}

void HdArnoldInstanceLightLinkingSplitSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    HdSceneIndexObserver::RemovedPrimEntries extraRemoved;

    for (const auto &entry : entries) {
        auto stateIt = _instancerCache.find(entry.primPath);
        if (stateIt == _instancerCache.end()) {
            continue;
        }

        // Emit PrimsRemoved for all synthetic prototype prims under this instancer.
        for (auto &[origRoot, split] : stateIt->second.protoSplits) {
            if (split.groups.size() <= 1) {
                continue;
            }
            std::vector<std::pair<SdfPath, TfToken>> origHierarchy;
            _CollectHierarchy(origRoot, origHierarchy);

            for (size_t gi = 1; gi < split.groups.size(); ++gi) {
                const SdfPath &synRoot = split.groups[gi].protoRootPath;
                for (auto &[origPath, primType] : origHierarchy) {
                    extraRemoved.push_back({origPath.ReplacePrefix(origRoot, synRoot)});
                }
            }
        }

        _RemoveFromCache(entry.primPath);
    }

    _SendPrimsRemoved(entries);
    if (!extraRemoved.empty()) {
        _SendPrimsRemoved(extraRemoved);
    }
}

void HdArnoldInstanceLightLinkingSplitSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    HdSceneIndexObserver::AddedPrimEntries extraAdded;
    HdSceneIndexObserver::RemovedPrimEntries extraRemoved;
    HdSceneIndexObserver::DirtiedPrimEntries extraDirtied;

    static const HdDataSourceLocator kInstancerTopoLocator =
        HdInstancerTopologySchema::GetDefaultLocator();
    static const HdDataSourceLocator kInstanceCategoriesLocator =
        HdInstanceCategoriesSchema::GetDefaultLocator();

    for (const auto &entry : entries) {
        auto stateIt = _instancerCache.find(entry.primPath);
        if (stateIt != _instancerCache.end()) {
            const bool topoOrCatDirty =
                entry.dirtyLocators.Intersects(kInstancerTopoLocator) ||
                entry.dirtyLocators.Intersects(kInstanceCategoriesLocator);

            if (topoOrCatDirty) {
                // Collect old synthetic hierarchy for removal.
                for (auto &[origRoot, split] : stateIt->second.protoSplits) {
                    if (split.groups.size() <= 1) continue;
                    std::vector<std::pair<SdfPath, TfToken>> origHierarchy;
                    _CollectHierarchy(origRoot, origHierarchy);
                    for (size_t gi = 1; gi < split.groups.size(); ++gi) {
                        const SdfPath &synRoot = split.groups[gi].protoRootPath;
                        for (auto &[origPath, primType] : origHierarchy) {
                            extraRemoved.push_back({origPath.ReplacePrefix(origRoot, synRoot)});
                        }
                    }
                }

                _RemoveFromCache(entry.primPath);
                _PopulateCache(entry.primPath);

                auto newStateIt = _instancerCache.find(entry.primPath);
                if (newStateIt != _instancerCache.end()) {
                    for (auto &[origRoot, split] : newStateIt->second.protoSplits) {
                        if (split.groups.size() <= 1) continue;
                        std::vector<std::pair<SdfPath, TfToken>> origHierarchy;
                        _CollectHierarchy(origRoot, origHierarchy);
                        for (size_t gi = 1; gi < split.groups.size(); ++gi) {
                            const SdfPath &synRoot = split.groups[gi].protoRootPath;
                            for (auto &[origPath, primType] : origHierarchy) {
                                extraAdded.push_back({origPath.ReplacePrefix(origRoot, synRoot), primType});
                            }
                        }
                    }
                }
            }
            continue;
        }

        // Propagate dirtiness from original prototype prims to their synthetic counterparts.
        for (auto &[origRoot, splitEntry] : _splitOriginalRoots) {
            if (entry.primPath == origRoot || entry.primPath.HasPrefix(origRoot)) {
                auto instStateIt = _instancerCache.find(splitEntry.instancerPath);
                if (instStateIt == _instancerCache.end()) break;
                auto protoIt = instStateIt->second.protoSplits.find(origRoot);
                if (protoIt == instStateIt->second.protoSplits.end()) break;

                for (size_t gi = 1; gi < protoIt->second.groups.size(); ++gi) {
                    const SdfPath &synRoot = protoIt->second.groups[gi].protoRootPath;
                    const SdfPath synPath = entry.primPath.ReplacePrefix(origRoot, synRoot);
                    extraDirtied.push_back({synPath, entry.dirtyLocators});
                }
                break;
            }
        }
    }

    if (!extraRemoved.empty()) {
        _SendPrimsRemoved(extraRemoved);
    }
    _SendPrimsDirtied(entries);
    if (!extraDirtied.empty()) {
        _SendPrimsDirtied(extraDirtied);
    }
    if (!extraAdded.empty()) {
        _SendPrimsAdded(extraAdded);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Plugin
////////////////////////////////////////////////////////////////////////////////

HdSceneIndexBaseRefPtr HdArnoldInstanceLightLinkingSplitSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs)
{
    return HdArnoldInstanceLightLinkingSplitSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HdArnoldUSE_INSTANCE_LIGHT_LINKING_SPLIT
#endif // ENABLE_SCENE_INDEX
