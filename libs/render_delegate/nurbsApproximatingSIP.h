#pragma once

#include <pxr/pxr.h>
#if PXR_VERSION >= 2505

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

#endif // PXR_VERSION >= 2505
