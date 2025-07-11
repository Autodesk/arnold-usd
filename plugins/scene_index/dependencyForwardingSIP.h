#pragma once
#include "pxr/pxr.h"
#ifdef ENABLE_SCENE_INDEX // Hydra 2
#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdArnold_DependencyForwardingSceneIndexPlugin
///
/// Plugin adds a dependency forwarding scene index to the Arnold render
/// delegate to resolve any dependencies introduced by other scene indices.
///
class HdArnoldDependencyForwardingSceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldDependencyForwardingSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX // Hydra 2
