#include "coordSysSIP.h"

#ifdef ENABLE_SCENE_INDEX

#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hdsi/coordSysPrimSceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdArnoldCoordSysSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HdArnoldCoordSysSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // Run early so the coordSys prims are available to any downstream consumer.
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold",
        _tokens->sceneIndexPluginName,
        nullptr,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdArnoldCoordSysSceneIndexPlugin::HdArnoldCoordSysSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdArnoldCoordSysSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return HdsiCoordSysPrimSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX
