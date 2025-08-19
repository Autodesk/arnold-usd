#pragma once

#ifdef ENABLE_SCENE_INDEX
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/sceneIndexPlugin.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(HdArnoldMeshLightResolvingSceneIndex);

// Scene index to create an Arnold meshlight when the MeshLightAPI is applied on a mesh
// This is almost identical to the PRman meshlight resolving scene index.
class HdArnoldMeshLightResolvingSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    static HdArnoldMeshLightResolvingSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr &inputSceneIndex);

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    HdArnoldMeshLightResolvingSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex);
    ~HdArnoldMeshLightResolvingSceneIndex() = default;

    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override;

    void _PrimsRemoved(
        const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries) override;

    void _PrimsDirtied(
        const HdSceneIndexBase &sender, const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;

private:
    std::unordered_map<SdfPath, bool, SdfPath::Hash> _meshLights;
};

class HdArnoldMeshLightResolvingSceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldMeshLightResolvingSceneIndexPlugin() = default;

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX
