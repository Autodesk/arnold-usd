

#include "fixMaterialPruningSIP.h"

#ifdef ENABLE_SCENE_INDEX

#include <pxr/base/tf/envSetting.h>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/lazyContainerDataSource.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usdImaging/usdImaging/usdPrimInfoSchema.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((sceneIndexPluginName, "HdArnoldFixMaterialPruningSceneIndexPlugin")));

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldFixMaterialPruningSceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName, nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

namespace {
TF_DECLARE_REF_PTRS(_FixMaterialPruningSceneIndex);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/// \class _SceneIndex
///
///
///
class _FixMaterialPruningSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    static _FixMaterialPruningSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
    {
        return TfCreateRefPtr(new _FixMaterialPruningSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

        // Check prim type info
        const UsdImagingUsdPrimInfoSchema primInfo = UsdImagingUsdPrimInfoSchema::GetFromParent(prim.dataSource);
        if (primInfo && primInfo.GetTypeName()->GetTypedValue(0.0) == TfToken("Material") &&
            prim.primType != HdPrimTypeTokens->material) {
            prim.primType = HdPrimTypeTokens->material;
        }

        return prim;
    }

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _FixMaterialPruningSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
#if PXR_VERSION >= 2308
        SetDisplayName("Arnold: fix material pruning in prototypes");
#endif
    }

    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        if (!_IsObserved()) {
            return;
        }
        HdSceneIndexObserver::AddedPrimEntries _entries(entries);
        //
        for (auto &entry : _entries) {
            HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(entry.primPath);

            // Check prim type info
            const UsdImagingUsdPrimInfoSchema primInfo = UsdImagingUsdPrimInfoSchema::GetFromParent(prim.dataSource);
            if (primInfo && primInfo.GetTypeName()->GetTypedValue(0.0) == TfToken("Material") &&
                prim.primType != HdPrimTypeTokens->material) {
                entry.primType = HdPrimTypeTokens->material;
            }
        }

        // Check if prims added are light and have dependencies, keep the dependencies
        _SendPrimsAdded(_entries);
    }

    void _PrimsRemoved(const HdSceneIndexBase &sender, const HdSceneIndexObserver::RemovedPrimEntries &entries) override
    {
        if (!_IsObserved()) {
            return;
        }
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

} // namespace

HdArnoldFixMaterialPruningSceneIndexPlugin::HdArnoldFixMaterialPruningSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldFixMaterialPruningSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs)
{
    return _FixMaterialPruningSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PENABLE_SCENE_INDEX
