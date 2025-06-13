

#include "implicitSurfaceSIP.h"

#if PXR_VERSION >= 2505

#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hdsi/implicitSurfaceSceneIndex.h"

#include "pxr/base/tf/envSetting.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(HDPRMAN_TESSELLATE_IMPLICIT_SURFACES, false,
    "Tessellate implicit surfaces into meshes, "
    "instead of using Arnold implicits");

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

    // Prman natively supports various quadric primitives (including cone,
    // cylinder and sphere), generating them such that they are rotationally
    // symmetric about the Z axis. To support other spine axes, configure the
    // scene index to overload the transform to account for the change of basis.
    // For unsupported primitives such as capsules and cubes, generate the
    // mesh instead.
    // 
    HdDataSourceBaseHandle const axisToTransformSrc =
        HdRetainedTypedSampledDataSource<TfToken>::New(
            HdsiImplicitSurfaceSceneIndexTokens->axisToTransform);
    HdDataSourceBaseHandle const toMeshSrc =
        HdRetainedTypedSampledDataSource<TfToken>::New(
            HdsiImplicitSurfaceSceneIndexTokens->toMesh);

    static bool tessellate = true;
     //   (TfGetEnvSetting(HDPRMAN_TESSELLATE_IMPLICIT_SURFACES) == true);

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

HdSceneIndexBaseRefPtr
HdArnoldImplicitSurfaceSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return HdsiImplicitSurfaceSceneIndex::New(inputScene, inputArgs);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_VERSION >= 2208
