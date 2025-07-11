#pragma once

#include <pxr/pxr.h>
#ifdef ENABLE_SCENE_INDEX

#include <pxr/imaging/hd/sceneIndexPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldNurbsApproximatingSceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldNurbsApproximatingSceneIndexPlugin();
    ~HdArnoldNurbsApproximatingSceneIndexPlugin() override;

protected: // HdSceneIndexPlugin overrides
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr& inputScene, const HdContainerDataSourceHandle& inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX
