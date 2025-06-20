#include "nurbsApproximatingSIP.h"

#if PXR_VERSION >= 2505

#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hdsi/nurbsApproximatingSceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((sceneIndexPluginName, "HdArnoldNurbsApproximatingSceneIndexPlugin")));

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldNurbsApproximatingSceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName, nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdArnoldNurbsApproximatingSceneIndexPlugin::HdArnoldNurbsApproximatingSceneIndexPlugin() = default;

HdArnoldNurbsApproximatingSceneIndexPlugin::~HdArnoldNurbsApproximatingSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldNurbsApproximatingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputSceneIndex, const HdContainerDataSourceHandle& inputArgs)
{
    TF_UNUSED(inputArgs);
    return HdsiNurbsApproximatingSceneIndex::New(inputSceneIndex);
}

PXR_NAMESPACE_CLOSE_SCOPE
#endif // PXR_VERSION >= 2505