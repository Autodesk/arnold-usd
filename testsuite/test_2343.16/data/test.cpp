// Interactive edits of implicit shapes and of a native arnold prim: sphere radius, cube size, cylinder axis and height,
// cone radius, capsule axis, and the radius of an ArnoldSphere. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kSphere("/World/sphere");
const SdfPath kCube("/World/cube");
const SdfPath kCylinder("/World/cylinder");
const SdfPath kCone("/World/cone");
const SdfPath kCapsule("/World/capsule");
const SdfPath kArnoldSphere("/World/arnold_sphere");

// The shapes are on two rows of three, the top row at y = 0.9 and the bottom one at y = -0.9
const GfVec3d kSpherePosition(-2, 0.9, 0);
const GfVec3d kCubePosition(0, 0.9, 0);
const GfVec3d kCylinderPosition(2, 0.9, 0);
const GfVec3d kConePosition(-2, -0.9, 0);
const GfVec3d kCapsulePosition(0, -0.9, 0);
const GfVec3d kArnoldSpherePosition(2, -0.9, 0);

GfVec2f Offset(const GfVec3d& position, double x, double y)
{
    return ScreenPosition(position + GfVec3d(x, y, 0));
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        UsdShadeMaterial red = DefinePreviewMaterial(stage, SdfPath("/World/materials/red"), GfVec3f(0.8f, 0.05f, 0.05f));

        UsdGeomSphere::Define(stage, kSphere).CreateRadiusAttr().Set(0.5);
        UsdGeomCube::Define(stage, kCube).CreateSizeAttr().Set(1.0);
        // The cylinder, cone and capsule are along Z by default, facing the camera
        UsdGeomCylinder cylinder = UsdGeomCylinder::Define(stage, kCylinder);
        cylinder.CreateRadiusAttr().Set(0.2);
        cylinder.CreateHeightAttr().Set(1.6);
        UsdGeomCone cone = UsdGeomCone::Define(stage, kCone);
        cone.CreateRadiusAttr().Set(0.5);
        cone.CreateHeightAttr().Set(0.5);
        UsdGeomCapsule capsule = UsdGeomCapsule::Define(stage, kCapsule);
        capsule.CreateRadiusAttr().Set(0.3);
        capsule.CreateHeightAttr().Set(0.6);
        UsdPrim arnoldSphere = stage->DefinePrim(kArnoldSphere, TfToken("ArnoldSphere"));
        arnoldSphere.CreateAttribute(TfToken("arnold:radius"), SdfValueTypeNames->FloatArray).Set(VtFloatArray{0.5f});

        for (const auto& shape : {std::make_pair(kSphere, kSpherePosition), std::make_pair(kCube, kCubePosition),
                 std::make_pair(kCylinder, kCylinderPosition), std::make_pair(kCone, kConePosition),
                 std::make_pair(kCapsule, kCapsulePosition), std::make_pair(kArnoldSphere, kArnoldSpherePosition)}) {
            SetTranslate(stage, shape.first, shape.second);
            BindMaterial(stage, shape.first, red);
        }
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr,
            AllOf({ExpectColor(Color::Red, Offset(kSpherePosition, 0.35, 0)),
                ExpectColor(Color::Red, Offset(kCubePosition, 0.35, 0.35)),
                ExpectColor(Color::Empty, Offset(kCylinderPosition, 0.7, 0)),
                ExpectColor(Color::Red, Offset(kConePosition, 0.35, 0)),
                ExpectColor(Color::Empty, Offset(kCapsulePosition, 0.5, 0)),
                ExpectColor(Color::Red, Offset(kArnoldSpherePosition, 0.35, 0))})},
        {"sphere radius change",
            [](const UsdStageRefPtr& stage) { UsdGeomSphere::Get(stage, kSphere).GetRadiusAttr().Set(0.2); },
            AllOf({ExpectColor(Color::Red, Offset(kSpherePosition, 0, 0)),
                ExpectColor(Color::Empty, Offset(kSpherePosition, 0.35, 0))})},
        {"cube size change",
            [](const UsdStageRefPtr& stage) { UsdGeomCube::Get(stage, kCube).GetSizeAttr().Set(0.4); },
            AllOf({ExpectColor(Color::Red, Offset(kCubePosition, 0, 0)),
                ExpectColor(Color::Empty, Offset(kCubePosition, 0.35, 0.35))})},
        {"cylinder axis change, from Z to X",
            [](const UsdStageRefPtr& stage) {
                UsdGeomCylinder::Get(stage, kCylinder).GetAxisAttr().Set(UsdGeomTokens->x);
            },
            ExpectColor(Color::Red, Offset(kCylinderPosition, 0.7, 0))},
        {"cylinder height change",
            [](const UsdStageRefPtr& stage) { UsdGeomCylinder::Get(stage, kCylinder).GetHeightAttr().Set(0.6); },
            AllOf({ExpectColor(Color::Red, Offset(kCylinderPosition, 0, 0)),
                ExpectColor(Color::Empty, Offset(kCylinderPosition, 0.7, 0))})},
        {"cone radius change",
            [](const UsdStageRefPtr& stage) { UsdGeomCone::Get(stage, kCone).GetRadiusAttr().Set(0.2); },
            AllOf({ExpectColor(Color::Red, Offset(kConePosition, 0, 0)),
                ExpectColor(Color::Empty, Offset(kConePosition, 0.35, 0))})},
        {"capsule axis change, from Z to X",
            [](const UsdStageRefPtr& stage) {
                UsdGeomCapsule::Get(stage, kCapsule).GetAxisAttr().Set(UsdGeomTokens->x);
            },
            ExpectColor(Color::Red, Offset(kCapsulePosition, 0.5, 0))},
        {"ArnoldSphere radius change",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kArnoldSphere).GetAttribute(TfToken("arnold:radius")).Set(VtFloatArray{0.2f});
            },
            AllOf({ExpectColor(Color::Red, Offset(kArnoldSpherePosition, 0, 0)),
                ExpectColor(Color::Empty, Offset(kArnoldSpherePosition, 0.35, 0))})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "this test covers the hydra code paths"}};
    return Run(scenario);
}
