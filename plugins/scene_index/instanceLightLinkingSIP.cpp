#include "instanceLightLinkingSIP.h"

#ifdef ENABLE_SCENE_INDEX

#include <pxr/imaging/hd/version.h>

#if HD_API_VERSION >= 58
#include <pxr/imaging/hdsi/version.h>
#endif

#ifdef HdArnoldUSE_INSTANCE_LIGHT_LINKING_SPLIT

#include <pxr/imaging/hd/categoriesSchema.h>
#include <pxr/imaging/hd/dataSourceLegacyPrim.h>
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
#include <set>

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

// Instancers coming from a legacy scene delegate (scene index emulation) are skipped:
// their data is pulled from the delegate, which isn't queryable while it is still
// populating. Reading instanceCategories from _PrimsAdded during
// UsdImagingDelegate::_Populate throws std::out_of_range in UsdImagingInstanceAdapter.
static bool _IsLegacyInstancer(const HdInstancerTopologySchema &topoSchema)
{
    HdContainerDataSourceHandle topoContainer = topoSchema.GetContainer();
    if (!topoContainer) {
        return false;
    }
    HdBoolDataSourceHandle legacyDS =
        HdBoolDataSource::Cast(topoContainer->Get(HdLegacyFlagTokens->isLegacyInstancer));
    return legacyDS && legacyDS->GetTypedValue(0.0f);
}

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
    bool anyOverride = false;

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

        // The render delegate only reads the categories of the prototype prims (and of the
        // instancer), never instanceCategories. Native instance prototypes live outside the
        // stage namespace, so no light-link collection matches them. Even when all instances
        // share the same categories we need to push them onto the prototype.
        if (groups.size() == 1 && groups.begin()->first.empty()) {
            continue;
        }

        anyOverride = true;
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

    return anyOverride;
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
    _instancerCache[instancerPath] = state;

    for (auto &[origRoot, split] : state.protoSplits) {
        if (split.groups.empty()) {
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

void HdArnoldInstanceLightLinkingSplitSceneIndex::_TrackInstancer(
    const SdfPath &instancerPath, const HdInstancerTopologySchema &topoSchema)
{
    VtArray<SdfPath> locations;
    if (HdPathArrayDataSourceHandle locationsDS = topoSchema.GetInstanceLocations()) {
        locations = locationsDS->GetTypedValue(0.0f);
    }
    for (const SdfPath &location : locations) {
        _instanceLocationToInstancer[location] = instancerPath;
    }
    _trackedInstancers[instancerPath] = std::move(locations);
}

void HdArnoldInstanceLightLinkingSplitSceneIndex::_UntrackInstancer(const SdfPath &instancerPath)
{
    auto it = _trackedInstancers.find(instancerPath);
    if (it == _trackedInstancers.end()) {
        return;
    }
    for (const SdfPath &location : it->second) {
        auto locIt = _instanceLocationToInstancer.find(location);
        if (locIt != _instanceLocationToInstancer.end() && locIt->second == instancerPath) {
            _instanceLocationToInstancer.erase(locIt);
        }
    }
    _trackedInstancers.erase(it);
}

void HdArnoldInstanceLightLinkingSplitSceneIndex::_RecomputeInstancer(
    const SdfPath &instancerPath,
    HdSceneIndexObserver::RemovedPrimEntries &removed,
    HdSceneIndexObserver::AddedPrimEntries &added,
    HdSceneIndexObserver::DirtiedPrimEntries &dirtied)
{
    static const HdDataSourceLocator kCategoriesLocator = HdCategoriesSchema::GetDefaultLocator();

    // Remove the previous synthetic hierarchies, and dirty the categories of the original
    // prototype prims since their override may change.
    std::set<SdfPath> oldOrigRoots;
    auto stateIt = _instancerCache.find(instancerPath);
    if (stateIt != _instancerCache.end()) {
        for (auto &[origRoot, split] : stateIt->second.protoSplits) {
            if (split.groups.empty()) {
                continue;
            }
            oldOrigRoots.insert(origRoot);
            std::vector<std::pair<SdfPath, TfToken>> origHierarchy;
            _CollectHierarchy(origRoot, origHierarchy);
            for (auto &[origPath, primType] : origHierarchy) {
                dirtied.push_back({origPath, kCategoriesLocator});
            }
            for (size_t gi = 1; gi < split.groups.size(); ++gi) {
                const SdfPath &synRoot = split.groups[gi].protoRootPath;
                for (auto &[origPath, primType] : origHierarchy) {
                    removed.push_back({origPath.ReplacePrefix(origRoot, synRoot)});
                }
            }
        }
    }
    _RemoveFromCache(instancerPath);

    // The instance locations may have changed with the topology.
    _UntrackInstancer(instancerPath);
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(instancerPath);
    _TrackInstancer(instancerPath, HdInstancerTopologySchema::GetFromParent(prim.dataSource));

    _PopulateCache(instancerPath);

    // Add the new synthetic hierarchies, and dirty the categories of the newly
    // overridden original prototype prims (they might have been added before the instancer).
    stateIt = _instancerCache.find(instancerPath);
    if (stateIt == _instancerCache.end()) {
        return;
    }
    for (auto &[origRoot, split] : stateIt->second.protoSplits) {
        if (split.groups.empty()) {
            continue;
        }
        std::vector<std::pair<SdfPath, TfToken>> origHierarchy;
        _CollectHierarchy(origRoot, origHierarchy);
        if (oldOrigRoots.count(origRoot) == 0) {
            for (auto &[origPath, primType] : origHierarchy) {
                dirtied.push_back({origPath, kCategoriesLocator});
            }
        }
        for (size_t gi = 1; gi < split.groups.size(); ++gi) {
            const SdfPath &synRoot = split.groups[gi].protoRootPath;
            for (auto &[origPath, primType] : origHierarchy) {
                added.push_back({origPath.ReplacePrefix(origRoot, synRoot), primType});
            }
        }
    }
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

    HdSceneIndexObserver::RemovedPrimEntries extraRemoved;
    HdSceneIndexObserver::AddedPrimEntries extraAdded;
    HdSceneIndexObserver::DirtiedPrimEntries extraDirtied;

    for (const auto &entry : entries) {
        // Check whether this newly-added prim is an instancer we should split.
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(entry.primPath);
        if (!prim.dataSource) {
            continue;
        }
        // Check the topology first: it is safe to read on legacy instancers,
        // while instanceCategories isn't. The instance categories may not be
        // available yet (lights not added), so all instancers are tracked.
        HdInstancerTopologySchema topoSchema =
            HdInstancerTopologySchema::GetFromParent(prim.dataSource);
        if (!topoSchema || _IsLegacyInstancer(topoSchema)) {
            continue;
        }

        _RecomputeInstancer(entry.primPath, extraRemoved, extraAdded, extraDirtied);
    }

    if (!extraRemoved.empty()) {
        _SendPrimsRemoved(extraRemoved);
    }
    _SendPrimsAdded(entries);
    if (!extraAdded.empty()) {
        _SendPrimsAdded(extraAdded);
    }
    if (!extraDirtied.empty()) {
        _SendPrimsDirtied(extraDirtied);
    }
}

void HdArnoldInstanceLightLinkingSplitSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    HdSceneIndexObserver::RemovedPrimEntries extraRemoved;

    // Removal entries are subtrees: collect all the tracked instancers below them.
    std::vector<SdfPath> removedInstancers;
    for (const auto &entry : entries) {
        for (const auto &[instancerPath, locations] : _trackedInstancers) {
            if (instancerPath.HasPrefix(entry.primPath)) {
                removedInstancers.push_back(instancerPath);
            }
        }
    }

    for (const SdfPath &instancerPath : removedInstancers) {
        _UntrackInstancer(instancerPath);
        auto stateIt = _instancerCache.find(instancerPath);
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

        _RemoveFromCache(instancerPath);
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
    static const HdDataSourceLocator kCategoriesLocator =
        HdCategoriesSchema::GetDefaultLocator();

    std::set<SdfPath> instancersToRecompute;
    for (const auto &entry : entries) {
        if (_trackedInstancers.count(entry.primPath)) {
            if (entry.dirtyLocators.Intersects(kInstancerTopoLocator) ||
                entry.dirtyLocators.Intersects(kInstanceCategoriesLocator)) {
                instancersToRecompute.insert(entry.primPath);
            }
            if (_instancerCache.count(entry.primPath)) {
                continue;
            }
        }

        // The light linking scene index dirties the categories of the instance prims, while
        // the instanceCategories of their instancer is only dirtied by the dependency
        // forwarding scene index, which comes after us.
        if (entry.dirtyLocators.Intersects(kCategoriesLocator)) {
            auto locIt = _instanceLocationToInstancer.find(entry.primPath);
            if (locIt != _instanceLocationToInstancer.end()) {
                instancersToRecompute.insert(locIt->second);
            }
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

    for (const SdfPath &instancerPath : instancersToRecompute) {
        _RecomputeInstancer(instancerPath, extraRemoved, extraAdded, extraDirtied);
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

