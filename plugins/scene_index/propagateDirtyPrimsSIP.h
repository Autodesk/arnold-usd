
#pragma once

#ifdef ENABLE_SCENE_INDEX

#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/sceneIndexPlugin.h>
#include <pxr/pxr.h>
#include "api.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(PropagateDirtyPrimsSceneIndex);

// This scene index is an entry point for the arnold render delegate, it is basically used to
// invalidate prims from the RenderPass::_Execute function, to mimic the original hydra 1 behaviour.

class PropagateDirtyPrimsSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    static PropagateDirtyPrimsSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr &inputSceneIndex);
    HDSCENEINDEX_API
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;
    HDSCENEINDEX_API
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

    HDSCENEINDEX_API
    void DirtyPrims(const HdSceneIndexObserver::DirtiedPrimEntries &entries);

protected:
    HDSCENEINDEX_API
    PropagateDirtyPrimsSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex);
    HDSCENEINDEX_API
    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override;
    HDSCENEINDEX_API
    void _PrimsRemoved(
        const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries) override;
    HDSCENEINDEX_API
    void _PrimsDirtied(
        const HdSceneIndexBase &sender, const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;
    HDSCENEINDEX_API
    void _SystemMessage(const TfToken &messageType, const HdDataSourceBaseHandle &args) override;
};

class HdArnoldPropagateDirtyPrimsSceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldPropagateDirtyPrimsSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
#endif
