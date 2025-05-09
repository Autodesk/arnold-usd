
//#if PXR_VERSION >= 2402

#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include "extComputationPrimvarPruningSIP.h"
#include <pxr/pxr.h>

//#if PXR_VERSION >= 2402
#include <pxr/imaging/hdsi/extComputationPrimvarPruningSceneIndex.h>
//#endif

PXR_NAMESPACE_OPEN_SCOPE


TF_DEFINE_PRIVATE_TOKENS(_tokens, ((sceneIndexPluginName, "HdArnoldExtComputationPrimvarPruningSceneIndexPlugin")));

////////////////////////////////////////////////////////////////////////////////
// Plugin registrations
////////////////////////////////////////////////////////////////////////////////

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HdArnoldExtComputationPrimvarPruningSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // Needs to be inserted earlier to allow plugins that follow to transform
    // primvar data without having to concern themselves about computed
    // primvars, but also after the UsdSkel scene index filters
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName,
        nullptr, // no argument data necessary
        insertionPhase, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

////////////////////////////////////////////////////////////////////////////////
// Scene Index Implementations
////////////////////////////////////////////////////////////////////////////////

HdArnoldExtComputationPrimvarPruningSceneIndexPlugin::HdArnoldExtComputationPrimvarPruningSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldExtComputationPrimvarPruningSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs)
{
    TF_UNUSED(inputArgs);
//#if PXR_VERSION >= 2402
    return HdSiExtComputationPrimvarPruningSceneIndex::New(inputScene);
//#else
//    return inputScene;
//#endif
}

PXR_NAMESPACE_CLOSE_SCOPE

//#endif // PXR_VERSION >= 2402
