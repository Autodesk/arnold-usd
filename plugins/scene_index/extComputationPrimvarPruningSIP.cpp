
#include <pxr/pxr.h>
#include "api.h"
#ifdef ENABLE_SCENE_INDEX // Hydra 2

#include "extComputationPrimvarPruningSIP.h"
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/lazyContainerDataSource.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdsi/extComputationPrimvarPruningSceneIndex.h>
#include <pxr/usdImaging/usdImaging/usdPrimInfoSchema.h>
#include <pxr/usdImaging/usdSkelImaging/bindingSchema.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((sceneIndexPluginName, "HdArnoldExtComputationPrimvarPruningSceneIndexPlugin")));

TF_DECLARE_REF_PTRS(_NormalsPruningDataSource);

class _NormalsPruningDataSource final : public HdContainerDataSource {
public:
    HD_DECLARE_DATASOURCE(_NormalsPruningDataSource);

    _NormalsPruningDataSource(HdContainerDataSourceHandle const &input) : _input(input) {}

    HDSCENEINDEX_API
    TfTokenVector GetNames() override
    {
        if (!_input) {
            return {};
        }
        TfTokenVector names = _input->GetNames();
        names.erase(std::remove(names.begin(), names.end(), HdPrimvarsSchemaTokens->normals), names.end());
        names.erase(std::remove(names.begin(), names.end(), TfToken("N")), names.end());
        return names;
    }

    HDSCENEINDEX_API
    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (!_input) {
            return nullptr;
        }
        if (name == TfToken("N") || name == HdPrimvarsSchemaTokens->normals)
            return nullptr;

        return _input->Get(name);
    }

private:
    HdContainerDataSourceHandle const _input;
};

HdContainerDataSourceHandle _NormalsPruning(const HdContainerDataSourceHandle &primSource)
{
    return _NormalsPruningDataSource::New(primSource);
}

inline bool _PrimIsSkinnedMesh(const HdSceneIndexPrim &prim)
{
    if (prim.primType == HdPrimTypeTokens->mesh) {
        // const UsdImagingUsdPrimInfoSchema primInfo = UsdImagingUsdPrimInfoSchema::GetFromParent(prim.dataSource);
        // return primInfo.GetTypeName()->GetTypedValue(0.0) == TfToken("Skeleton");
        UsdSkelImagingBindingSchema usdSkelBindings = UsdSkelImagingBindingSchema::GetFromParent(prim.dataSource);
        if (usdSkelBindings) {
            const HdPathDataSourceHandle skeleton = usdSkelBindings.GetSkeleton();
            return skeleton && !skeleton->GetTypedValue(0.0).IsEmpty();
            // if (prim.primType == HdPrimTypeTokens->mesh) {
            //         UsdImagingUsdPrimInfoSchema primInfo = UsdImagingUsdPrimInfoSchema::GetFromParent(prim);
            //         if (primInfo && primInfo.GetTypeName() == TfToken(""))
            //     UsdSkelImagingBindingSchema usdSkelBindings =
            //     UsdSkelImagingBindingSchema::GetFromParent(prim.dataSource); if (usdSkelBindings) {
            // const HdBoolDataSourceHandle hasSkelRoot = usdSkelBindings.GetHasSkelRoot();
            // return hasSkelRoot && hasSkelRoot->GetTypedValue(0.0);
        }
    }

    return false;
}

TF_DECLARE_REF_PTRS(_ExtComputationNormalsPruningSceneIndex);

/// Scene index filter that removes normals from meshes used for usdSkel extComputation
/// We want to let arnold always recompute the normals of the skinned mesh
class _ExtComputationNormalsPruningSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    std::unordered_map<SdfPath, SdfPath, SdfPath::Hash> _lightDep;
    static _ExtComputationNormalsPruningSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
    {
        return TfCreateRefPtr(new _ExtComputationNormalsPruningSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override
    {
        const HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
        if (_PrimIsSkinnedMesh(prim)) {
            return {
                prim.primType,
                HdContainerDataSourceEditor(prim.dataSource)
                    .Set(
                        HdPrimvarsSchema::GetDefaultLocator(),
                        HdLazyContainerDataSource::New(std::bind(
                            _NormalsPruning,
                            HdContainerDataSource::Cast(prim.dataSource->Get(HdPrimvarsSchemaTokens->primvars)))))
                    .Finish()};
        }
        return prim;
    }

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _ExtComputationNormalsPruningSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
#if PXR_VERSION >= 2308
        SetDisplayName("Arnold: prune skinned mesh normals");
#endif
    }

    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        _SendPrimsAdded(entries);
    }

    void _PrimsRemoved(const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries) override
    {
        _SendPrimsRemoved(entries);
    }

    void _PrimsDirtied(const HdSceneIndexBase &sender, const HdSceneIndexObserver::DirtiedPrimEntries &entries) override
    {
        if (!_IsObserved()) {
            return;
        }

        _SendPrimsDirtied(entries);
    }
};

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
    return HdSiExtComputationPrimvarPruningSceneIndex::New(_ExtComputationNormalsPruningSceneIndex::New(inputScene));
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX Hydra 2
