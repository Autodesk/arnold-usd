// Interactive edits of an arnold primvar of a light: primvars:arnold:cast_shadows is added to a sphere light and
// removed, then the light intensity is edited. Since #2681, HdArnoldGenericLight::Sync doesn't reset the light node
// anymore, so a removed arnold primvar must get its default value back some other way. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdLux/sphereLight.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kWall("/World/wall");
const SdfPath kOccluder("/World/occluder");
const SdfPath kLight("/World/light");
const TfToken kCastShadows("arnold:cast_shadows");

// A quad facing +Z, centered on the prim origin
void DefineQuad(const UsdStageRefPtr& stage, const SdfPath& path, float halfSize, const GfVec3d& position)
{
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, path);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    mesh.CreatePointsAttr().Set(VtVec3fArray{GfVec3f(-halfSize, -halfSize, 0.f), GfVec3f(halfSize, -halfSize, 0.f),
        GfVec3f(halfSize, halfSize, 0.f), GfVec3f(-halfSize, halfSize, 0.f)});
    mesh.CreateFaceVertexCountsAttr().Set(VtIntArray{4});
    mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray{0, 1, 2, 3});
    SetTranslate(stage, path, position);
}

UsdGeomPrimvarsAPI LightPrimvars(const UsdStageRefPtr& stage)
{
    return UsdGeomPrimvarsAPI(stage->GetPrimAtPath(kLight));
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    // The light is at (2, 0, 2) and the occluder halfway to the center of the wall, which is in its shadow. Seen from
    // the camera, the occluder is on the right of the center.
    const GfVec2f shadow = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f lit = ScreenPosition(GfVec3d(-1, 0.6, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineQuad(stage, kWall, 2.5f, GfVec3d(0, 0, 0));
        DefineQuad(stage, kOccluder, 0.2f, GfVec3d(1, 0, 1));
        UsdLuxSphereLight light = UsdLuxSphereLight::Define(stage, kLight);
        light.CreateRadiusAttr().Set(0.05f);
        light.CreateIntensityAttr().Set(30.f);
        light.CreateNormalizeAttr().Set(true);
        SetTranslate(stage, kLight, GfVec3d(2, 0, 2));
        return stage;
    };

    scenario.steps = {
        {"initial render, the center of the wall is in the shadow of the occluder", nullptr,
            AllOf({ExpectColor(Color::Black, shadow), ExpectColor(Color::Grey, lit)})},
        {"primvars:arnold:cast_shadows set to false on the light",
            [](const UsdStageRefPtr& stage) {
                LightPrimvars(stage)
                    .CreatePrimvar(kCastShadows, SdfValueTypeNames->Bool, UsdGeomTokens->constant)
                    .Set(false);
            },
            AllOf({ExpectColor(Color::Grey, shadow), ExpectColor(Color::Grey, lit)})},
        {"primvars:arnold:cast_shadows removed, back to the default",
            [](const UsdStageRefPtr& stage) { LightPrimvars(stage).RemovePrimvar(kCastShadows); },
            AllOf({ExpectColor(Color::Black, shadow), ExpectColor(Color::Grey, lit)})},
        {"light intensity change, the light keeps its transform",
            [](const UsdStageRefPtr& stage) { UsdLuxSphereLight::Get(stage, kLight).GetIntensityAttr().Set(20.f); },
            AllOf({ExpectColor(Color::Black, shadow), ExpectColor(Color::Grey, lit)})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "this test covers the hydra code paths"}};
    return Run(scenario);
}
