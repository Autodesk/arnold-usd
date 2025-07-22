
#include <pxr/pxr.h>
#ifdef ENABLE_SCENE_INDEX // Hydra2

#include "pxr/imaging/hd/version.h"


// There was no hdsi/version.h before this HD_API_VERSION.
#if HD_API_VERSION >= 58

#include "pxr/imaging/hdsi/version.h"

#if HDSI_API_VERSION >= 13
#define HdArnoldUSE_LIGHT_LINKING_SCENE_INDEX
#endif

#endif // #if HD_API_VERSION >= 58

#ifdef HdArnoldUSE_LIGHT_LINKING_SCENE_INDEX

#include "pxr/imaging/hd/sceneIndexPlugin.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hdsi/lightLinkingSceneIndex.h"

#include "pxr/base/tf/envSetting.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((sceneIndexPluginName, "HdArnoldLightLinkingSceneIndexPlugin")));

TF_DEFINE_ENV_SETTING(
    HdArnoldENABLE_LIGHT_LINKING_SCENE_INDEX, true, "Enable registration for the light linking scene index.");

////////////////////////////////////////////////////////////////////////////////
// Plugin registration
////////////////////////////////////////////////////////////////////////////////

class HdArnoldLightLinkingSceneIndexPlugin;

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldLightLinkingSceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // XXX Picking an arbitrary phase for now. If a procedural were to
    //     generate light prims, we'd want this to be after it.
    //     HdGpSceneIndexPlugin::GetInsertionPhase() currently returns 2.
    //
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 4;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName,
        nullptr, insertionPhase, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

////////////////////////////////////////////////////////////////////////////////
// Scene Index Implementation
////////////////////////////////////////////////////////////////////////////////

class HdArnoldLightLinkingSceneIndexPlugin : public HdSceneIndexPlugin {
public:
    HdArnoldLightLinkingSceneIndexPlugin() = default;

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs) override
    {
        return HdsiLightLinkingSceneIndex::New(inputScene, inputArgs);
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

#endif // ENABLE_SCENE_INDEX // Hydra2
