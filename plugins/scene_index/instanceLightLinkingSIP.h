#pragma once

#ifdef ENABLE_SCENE_INDEX

#include <pxr/imaging/hd/version.h>

#if HD_API_VERSION >= 58
#include <pxr/imaging/hdsi/version.h>
#if HDSI_API_VERSION >= 13
#define HdArnoldUSE_INSTANCE_LIGHT_LINKING_SPLIT
#endif
#endif

#ifdef HdArnoldUSE_INSTANCE_LIGHT_LINKING_SPLIT

#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/sceneIndexPlugin.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>

#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(HdArnoldInstanceLightLinkingSplitSceneIndex);

/// Scene index filter (phase 5, after HdsiLightLinkingSceneIndex at phase 4) that splits
/// point-instancer prototypes by their per-instance light-link categories so that each
/// group of instances with a unique category set gets its own Arnold shape node.
class HdArnoldInstanceLightLinkingSplitSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    static HdArnoldInstanceLightLinkingSplitSceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr &inputSceneIndex);

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    explicit HdArnoldInstanceLightLinkingSplitSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex);

    void _PrimsAdded(const HdSceneIndexBase &sender,
                     const HdSceneIndexObserver::AddedPrimEntries &entries) override;
    void _PrimsRemoved(const HdSceneIndexBase &sender,
                       const HdSceneIndexObserver::RemovedPrimEntries &entries) override;
    void _PrimsDirtied(const HdSceneIndexBase &sender,
                       const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;

private:
    using CategoryKey = std::vector<TfToken>;

    struct GroupInfo {
        CategoryKey categories;
        SdfPath protoRootPath;  // original path for group 0, synthetic for groups 1+
        VtIntArray indices;     // instance data slot indices for this group
    };

    struct PrototypeSplit {
        std::vector<GroupInfo> groups;  // groups.size() > 1 means splitting is needed
    };

    struct InstancerSplitState {
        std::unordered_map<SdfPath, PrototypeSplit, SdfPath::Hash> protoSplits;
    };

    struct SyntheticRootEntry {
        SdfPath instancerPath;
        SdfPath originalRootPath;
        CategoryKey groupCategories;
    };

    struct SplitOriginalEntry {
        SdfPath instancerPath;
        CategoryKey group0Categories;
    };

    bool _ComputeSplitState(const SdfPath &instancerPath,
                            const HdSceneIndexPrim &prim,
                            InstancerSplitState &outState) const;

    void _PopulateCache(const SdfPath &instancerPath);
    void _RemoveFromCache(const SdfPath &instancerPath);

    void _CollectHierarchy(const SdfPath &rootPath,
                           std::vector<std::pair<SdfPath, TfToken>> &outPrims) const;

    HdSceneIndexPrim _GetModifiedInstancerPrim(const SdfPath &path,
                                               const InstancerSplitState &state) const;

    static HdContainerDataSourceHandle _BuildCategoriesOverlay(
        const HdContainerDataSourceHandle &originalDS,
        const CategoryKey &categories);

    static HdContainerDataSourceHandle _BuildInstancedByOverlay(
        const HdContainerDataSourceHandle &originalDS,
        const SdfPath &newPrototypeRoot);

    static SdfPath _MakeSyntheticRootPath(const SdfPath &originalPath, size_t groupIndex);

    std::unordered_map<SdfPath, InstancerSplitState, SdfPath::Hash> _instancerCache;
    std::unordered_map<SdfPath, SyntheticRootEntry, SdfPath::Hash> _syntheticRoots;
    std::unordered_map<SdfPath, std::vector<SdfPath>, SdfPath::Hash> _parentToSyntheticRoots;
    std::unordered_map<SdfPath, SplitOriginalEntry, SdfPath::Hash> _splitOriginalRoots;
};

class HdArnoldInstanceLightLinkingSplitSceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldInstanceLightLinkingSplitSceneIndexPlugin() = default;

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HdArnoldUSE_INSTANCE_LIGHT_LINKING_SPLIT
#endif // ENABLE_SCENE_INDEX
