

#include "implicitSurfaceSIP.h"

#ifdef ENABLE_SCENE_INDEX

#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdsi/implicitSurfaceSceneIndex.h>
#include <pxr/usdImaging/usdImaging/usdPrimInfoSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/meshTopologySchema.h>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/base/tf/envSetting.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdArnoldImplicitSurfaceSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HdArnoldImplicitSurfaceSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    HdDataSourceBaseHandle const axisToTransformSrc =
        HdRetainedTypedSampledDataSource<TfToken>::New(
            HdsiImplicitSurfaceSceneIndexTokens->axisToTransform);
    HdDataSourceBaseHandle const toMeshSrc =
        HdRetainedTypedSampledDataSource<TfToken>::New(
            HdsiImplicitSurfaceSceneIndexTokens->toMesh);

    static bool tessellate = true;

    HdContainerDataSourceHandle inputArgs;
    if (tessellate) {
        // Tessellate everything (legacy behavior).
        inputArgs =
            HdRetainedContainerDataSource::New(
                HdPrimTypeTokens->sphere, toMeshSrc,
                HdPrimTypeTokens->cube, toMeshSrc,
                HdPrimTypeTokens->cone, toMeshSrc,
                HdPrimTypeTokens->cylinder, toMeshSrc,
#if PXR_VERSION >= 2411
                HdPrimTypeTokens->capsule, toMeshSrc,
                HdPrimTypeTokens->plane, toMeshSrc);
#else
                HdPrimTypeTokens->capsule, toMeshSrc);
#endif
    } else {
        // Cone and cylinder need transforms updated, and cube and capsule
        // and plane still need to be tessellated.
        inputArgs =
            HdRetainedContainerDataSource::New(
                HdPrimTypeTokens->cone, axisToTransformSrc,
                HdPrimTypeTokens->cylinder, axisToTransformSrc,
                HdPrimTypeTokens->cube, toMeshSrc,
#if PXR_VERSION >= 2411
                HdPrimTypeTokens->capsule, toMeshSrc,
                HdPrimTypeTokens->plane, toMeshSrc);
#else
                HdPrimTypeTokens->capsule, toMeshSrc);
#endif
    }

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
                "Arnold",
                _tokens->sceneIndexPluginName,
                inputArgs,
                insertionPhase,
                HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdArnoldImplicitSurfaceSceneIndexPlugin::
HdArnoldImplicitSurfaceSceneIndexPlugin() = default;

namespace {
TF_DECLARE_REF_PTRS(_FixImplicitSurfaceSidedNessSceneIndex);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/// \class _FixImplicitSurfaceSidedNessSceneIndex
///
/// The following scene index forces the sidedness of closed implicit surfaces. The ability to set the doubleSided
/// attribute was removed in 25.05 and is causing issues in Arnold when we apply CSG operators to those geometries.
/// For now the solution is to force all closed implicit geometries to be doubleSided, this is the purpose of
/// _FixImplicitSurfaceSidedNessSceneIndex Hopefully this should be fixed in future versions of USD.
///
class _FixImplicitSurfaceSidedNessSceneIndex : public HdSingleInputFilteringSceneIndexBase {
public:

    static _FixImplicitSurfaceSidedNessSceneIndexRefPtr New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
    {
        return TfCreateRefPtr(new _FixImplicitSurfaceSidedNessSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

        // Check prim type info
        const UsdImagingUsdPrimInfoSchema primInfo = UsdImagingUsdPrimInfoSchema::GetFromParent(prim.dataSource);
        if (primInfo && primInfo.GetTypeName()) {
            // Force all closed implicit surfaces to be doubleSided.
            const TfToken &typeName = primInfo.GetTypeName()->GetTypedValue(0.0);
            if (typeName == TfToken("Cube") || typeName == TfToken("Cone") || typeName == TfToken("Cylinder") ||
                typeName == TfToken("Capsule") || typeName == TfToken("Sphere")) {
                HdContainerDataSourceHandle meshDataSource =
                    HdContainerDataSource::Cast(prim.dataSource->Get(HdMeshSchema::GetSchemaToken()));
                if (meshDataSource) {
                    return {
                        prim.primType, HdContainerDataSourceEditor(prim.dataSource)
                                           .Overlay(
                                               HdMeshSchema::GetDefaultLocator(),
                                               HdContainerDataSourceEditor(meshDataSource)
                                                   .Set(
                                                       HdDataSourceLocator(HdMeshSchemaTokens->doubleSided),
                                                       HdRetainedTypedSampledDataSource<bool>::New(true))
                                                   .Finish())
                                           .Finish()};
                }
            }
        }
        return prim;
    }

    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _FixImplicitSurfaceSidedNessSceneIndex(const HdSceneIndexBaseRefPtr &inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
#if PXR_VERSION >= 2308
        SetDisplayName("Arnold: fix closed implicit surface sidedness");
#endif
    }

    void _PrimsAdded(const HdSceneIndexBase &sender, const HdSceneIndexObserver::AddedPrimEntries &entries) override
    {
        if (!_IsObserved()) {
            return;
        }
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


HdSceneIndexBaseRefPtr
HdArnoldImplicitSurfaceSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    auto implicitSurfaceSceneIndex = HdsiImplicitSurfaceSceneIndex::New(inputScene, inputArgs);
#if PXR_VERSION <= 2505
    implicitSurfaceSceneIndex->SetDisplayName("Arnold: implicit surface scene index");
    return _FixImplicitSurfaceSidedNessSceneIndex::New(implicitSurfaceSceneIndex);
#else // Assuming the sidedness bug is fixed in 25.08
    return implicitSurfaceSceneIndex;
#endif    
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // ENABLE_SCENE_INDEX
