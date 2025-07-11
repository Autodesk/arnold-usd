#pragma once
//
// This scene index declares the dependencies needed in the arnold usd nodes to correctly invalidates the prims
//
#include <pxr/pxr.h>

#ifdef ENABLE_SCENE_INDEX // Hydra 2

#include <pxr/imaging/hd/sceneIndexPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldDependencySceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldDependencySceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs) override;

};

PXR_NAMESPACE_CLOSE_SCOPE
#endif // ENABLE_SCENE_INDEX // Hydra 2