#include "meshLightResolvingSIP.h"
#ifdef ENABLE_SCENE_INDEX
#include <pxr/pxr.h>

#include <pxr/imaging/hd/filteringSceneIndex.h>

// Schemas
#include <pxr/imaging/hd/categoriesSchema.h>
#include <pxr/imaging/hd/lightSchema.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/materialNetworkSchema.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/visibilitySchema.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/usdImaging/usdImaging/modelSchema.h>

// Data sources
#include <pxr/imaging/hd/dataSourceMaterialNetworkInterface.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>

#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>

// Tokens
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usdLux/tokens.h>

#include <pxr/imaging/hd/version.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens, ((sceneIndexPluginName, "HdArnoldMeshLightResolvingSceneIndexPlugin"))((lightName, "arnoldMeshLight")));

// Utils
namespace {

inline bool _PrimTypeIsCompatibleWithMeshLight(const TfToken &primType)
{
    return (primType == HdPrimTypeTokens->mesh);
    // TODO should the volume be associated to arnold mesh_light node ?
    //|| (prim.primType == HdPrimTypeTokens->volume)
}

bool _IsMeshLight(const HdSceneIndexPrim &prim)
{
    if (_PrimTypeIsCompatibleWithMeshLight(prim.primType)) {
        if (auto lightSchema = HdLightSchema::GetFromParent(prim.dataSource)) {
            if (auto dataSource = HdBoolDataSource::Cast(lightSchema.GetContainer()->Get(HdTokens->isLight))) {
                return dataSource->GetTypedValue(0.0f);
            }
        }
    }
    return false;
}

TfToken _GetMaterialSyncMode(const HdContainerDataSourceHandle &primDs)
{
    const static TfToken defaultMaterialSyncMode = UsdLuxTokens->materialGlowTintsLight;

    if (auto lightSchema = HdLightSchema::GetFromParent(primDs)) {
        if (auto dataSource = HdTokenDataSource::Cast(

                lightSchema.GetContainer()->Get(HdTokens->materialSyncMode))) {
            const TfToken materialSyncMode = dataSource->GetTypedValue(0.0f);
            return materialSyncMode.IsEmpty() ? defaultMaterialSyncMode : materialSyncMode;
        }
    }
    return defaultMaterialSyncMode;
}

HdContainerDataSourceHandle _BuildLightDataSource(
    const SdfPath &originPath, const HdSceneIndexPrim &originPrim, const SdfPath &bindingSourcePath,
    const HdContainerDataSourceHandle &bindingSourceDS, const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    const TfToken materialSyncMode = _GetMaterialSyncMode(originPrim.dataSource);
    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> sources;

    // Knock out primvars
    names.push_back(HdPrimvarsSchemaTokens->primvars);
    sources.push_back(HdBlockDataSource::New());

    // Knock out model
    names.push_back(UsdImagingModelSchemaTokens->model);
    sources.push_back(HdBlockDataSource::New());

    // Knock out mesh
    names.push_back(HdMeshSchemaTokens->mesh);
    sources.push_back(HdBlockDataSource::New());

    if (originPrim.primType != HdPrimTypeTokens->volume) {
        // Knock out material binding
        names.push_back(HdMaterialBindingsSchema::GetSchemaToken());
        sources.push_back(HdBlockDataSource::New());
    }

    // Knock out volume field binding
    // names.push_back(HdVolumeFieldBindingSchemaTokens->volumeFieldBinding);
    // sources.push_back(HdBlockDataSource::New());

    HdContainerDataSourceHandle handles[2] = {
        HdRetainedContainerDataSource::New(names.size(), names.data(), sources.data()), originPrim.dataSource};

    return HdOverlayContainerDataSource::New(2, handles);
}

} // namespace
HdArnoldMeshLightResolvingSceneIndex::HdArnoldMeshLightResolvingSceneIndex(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
    SetDisplayName("Arnold: mesh lights");
}

HdArnoldMeshLightResolvingSceneIndexRefPtr HdArnoldMeshLightResolvingSceneIndex::New(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    return TfCreateRefPtr(new HdArnoldMeshLightResolvingSceneIndex(inputSceneIndex));
}

HdSceneIndexPrim HdArnoldMeshLightResolvingSceneIndex::GetPrim(const SdfPath &primPath) const
{
    // Are we on a meshLight under a mesh ?
    const SdfPath &parentPath = primPath.GetParentPath();
    if (_meshLights.count(parentPath) > 0) {
        const HdSceneIndexPrim &parentPrim = _GetInputSceneIndex()->GetPrim(parentPath);

        //
        if (primPath.GetNameToken() == _tokens->lightName) {
            return {
                HdPrimTypeTokens->meshLight,
                _BuildLightDataSource(
                    parentPath, parentPrim, parentPath, parentPrim.dataSource, // materialBinding source
                    _GetInputSceneIndex())};
        }
    }
    return _GetInputSceneIndex()->GetPrim(primPath);
}

SdfPathVector HdArnoldMeshLightResolvingSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const
{
    SdfPathVector paths = _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    if (_meshLights.count(primPath)) {
        paths.push_back(primPath.AppendChild(_tokens->lightName));
    }
    return paths;
}

void HdArnoldMeshLightResolvingSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }
    HdSceneIndexObserver::AddedPrimEntries added;

    // When a mesh light is added, we create a meshLight hydra prim under the mesh
    // This will ultimately create a light Sprim and an
    for (const auto &entry : entries) {
        if (_PrimTypeIsCompatibleWithMeshLight(entry.primType)) {
            HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(entry.primPath);

            // Doc from PRman
            // The prim is a mesh light if light.isLight is true. But a mesh
            // light also needs a valid light shader network [material
            // resource], which it won't have when stage scene index is not
            // enabled.
            //
            // Mesh lights are not supported without stage scene index;
            // we should not insert the light, source, or stripped-down mesh
            // unless it is enabled. If it is disabled, we should instead
            // just forward the origin prim along unmodified at its original
            // path; downstream HdPrman will treat it as the mesh its prim type
            // declares it to be.

            if (_IsMeshLight(prim)) {
                // const bool meshVisible = _GetMaterialSyncMode(prim.dataSource) != UsdLuxTokens->noMaterialResponse;
                _meshLights.insert({entry.primPath, true}); // TODO meshVisible
                // The light prim
                added.emplace_back(entry.primPath.AppendChild(_tokens->lightName), HdPrimTypeTokens->meshLight);
            }
        }
        added.push_back(entry);
    }
    _SendPrimsAdded(added);
}

void HdArnoldMeshLightResolvingSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    HdSceneIndexObserver::RemovedPrimEntries removed;

    for (const auto &entry : entries) {
        if (_meshLights.count(entry.primPath)) {
            // The light prim
            removed.emplace_back(entry.primPath.AppendChild(_tokens->lightName));
            _meshLights.erase(entry.primPath);
            continue;
        }
        removed.push_back(entry);
    }
    _SendPrimsRemoved(removed);
}

void HdArnoldMeshLightResolvingSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender, const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    // Parameter change on the meshLight
    for (const auto &entry : entries) {
        if (_meshLights.count(entry.primPath)) {
            // Propogate dirtiness to the meshLight light if applicable.
            // HdDataSourceLocator::EmptyLocator() == AllDirty in Hydra 1.0
            if (entry.dirtyLocators.Intersects(HdDataSourceLocator::EmptyLocator()) ||
                entry.dirtyLocators.Intersects(HdCategoriesSchema::GetDefaultLocator()) ||
                entry.dirtyLocators.Intersects(HdMaterialBindingsSchema::GetDefaultLocator())) {
                _SendPrimsDirtied(
                    {{entry.primPath.AppendChild(_tokens->lightName),
                      {HdLightSchema::GetDefaultLocator(), HdMaterialSchema::GetDefaultLocator(),
                       HdPrimvarsSchema::GetDefaultLocator(), HdVisibilitySchema::GetDefaultLocator(),
                       HdXformSchema::GetDefaultLocator()}}});
            }
        }
    }
    _SendPrimsDirtied(entries);
}

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldMeshLightResolvingSceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // We need an "insertion point" that's *after* general material resolve.
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 115;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName,
        nullptr, // No input args.
        insertionPhase, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdSceneIndexBaseRefPtr HdArnoldMeshLightResolvingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs)
{
    return HdArnoldMeshLightResolvingSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif