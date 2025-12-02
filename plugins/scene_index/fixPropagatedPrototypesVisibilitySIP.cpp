#include "fixPropagatedPrototypesVisibilitySIP.h"

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
#include <pxr/imaging/hd/visibilitySchema.h> 
#include <pxr/usdImaging/usdImaging/usdPrimInfoSchema.h>
#include <pxr/imaging/hd/primOriginSchema.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((sceneIndexPluginName, "HdArnoldPropagatedPrototypesVisibilitySceneIndexPlugin")));

TF_REGISTRY_FUNCTION(TfType) { HdSceneIndexPluginRegistry::Define<HdArnoldPropagatedPrototypesVisibilitySceneIndexPlugin>(); }

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Arnold", _tokens->sceneIndexPluginName, nullptr, insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

namespace {
TF_DECLARE_REF_PTRS(_FixPropagatedPrototypesSceneIndex);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/// \class _FixPropagatedPrototypesSceneIndex
/// Fix the visibility of propagated prototypes. We might need new tests to ensure this scene index does not interfere
/// with the visibility set by the point instancer.
///
class _FixPropagatedPrototypesSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:
    static _FixPropagatedPrototypesSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
    {
        return TfCreateRefPtr(new _FixPropagatedPrototypesSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
        // Use the USD Imaging schema to access primOrigin
        HdPrimOriginSchema primOriginSchema = HdPrimOriginSchema::GetFromParent(prim.dataSource);
        if (!primOriginSchema) {
            return prim;
        }
        // Get the originating prim, make sure it is not a relative path
        SdfPath originPath = primOriginSchema.GetOriginPath(HdPrimOriginSchemaTokens->scenePath);
        if (!originPath.IsAbsolutePath()) {
            originPath = originPath.MakeAbsolutePath(primPath);
        }
        HdSceneIndexPrim originPrim = _GetInputSceneIndex()->GetPrim(originPath);
        if (!originPrim.dataSource) {
            return prim;
        }
        // Check if the originating prim has a visibility value
        HdBoolDataSourceHandle visibilityDs = HdVisibilitySchema::GetFromParent(originPrim.dataSource).GetVisibility();
        if (!visibilityDs) {
            return prim;
        }
        // Set the propagated prototype's visibility to match the originating prim and return the modified prim
        return {prim.primType, 
            HdContainerDataSourceEditor(prim.dataSource)
                    .Overlay(
                        HdVisibilitySchema::GetDefaultLocator(),
                        HdVisibilitySchema::Builder()
                            .SetVisibility(visibilityDs)
                            .Build())
                    .Finish() };
    }

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _FixPropagatedPrototypesSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
#if PXR_VERSION >= 2308
        SetDisplayName("Arnold: fix propagated prototypes visibility");
#endif
    }

    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        if (!_IsObserved()) {
            return;
        }

        // Check if prims added are light and have dependencies, keep the dependencies
        _SendPrimsAdded(entries);
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

HdArnoldPropagatedPrototypesVisibilitySceneIndexPlugin::HdArnoldPropagatedPrototypesVisibilitySceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdArnoldPropagatedPrototypesVisibilitySceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle &inputArgs)
{
    return _FixPropagatedPrototypesSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX
