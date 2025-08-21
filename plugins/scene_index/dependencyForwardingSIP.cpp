#include "dependencyForwardingSIP.h"

#ifdef ENABLE_SCENE_INDEX // Hydra 2
#include "pxr/imaging/hd/dependencyForwardingSceneIndex.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((sceneIndexPluginName, "HdArnoldDependencyForwardingSceneIndexPlugin")));

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldDependencyForwardingSceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 1000;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName, nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtEnd);
}

HdArnoldDependencyForwardingSceneIndexPlugin::HdArnoldDependencyForwardingSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldDependencyForwardingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs)
{
    auto sceneIndexFilter = HdDependencyForwardingSceneIndex::New(inputScene);
    sceneIndexFilter->SetDisplayName("Arnold: forward dependencies");
    return sceneIndexFilter;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX // Hydra 2