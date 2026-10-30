// Interactive edits of prims and materials: a sphere in front of the camera has its material changed, moves out of
// the frame and back, and a cube is added and removed. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/shader.h>

using namespace interactive_usd_test;

int main(int, char**)
{
    const SdfPath sphere("/World/sphere");
    const SdfPath cube("/World/cube");

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        UsdGeomSphere::Define(stage, sphere).CreateRadiusAttr().Set(1.0);
        SetTranslate(stage, sphere, GfVec3d(0, 0, 0));
        BindMaterial(stage, sphere,
            DefinePreviewMaterial(stage, SdfPath("/World/materials/sphere_mat"), GfVec3f(0.8f, 0.05f, 0.05f)));
        return stage;
    };

    const char* legacyReaderIssue = "the legacy reader doesn't move the sphere out of the frame (previous step), creates "
                                    "the new prim twice and doesn't delete removed prims";
    scenario.steps = {
        {"initial render", nullptr, ExpectColor(Color::Red)},
        {"material attribute change",
            [](const UsdStageRefPtr& stage) {
                UsdShadeShader::Get(stage, SdfPath("/World/materials/sphere_mat/surface"))
                    .GetInput(TfToken("diffuseColor"))
                    .Set(GfVec3f(0.05f, 0.8f, 0.05f));
            },
            ExpectColor(Color::Green)},
        {"transform change, moving the sphere out of the frame",
            [&](const UsdStageRefPtr& stage) { SetTranslate(stage, sphere, GfVec3d(0, 10, 0)); },
            ExpectColor(Color::Empty),
            {{ProceduralLegacyReader, "the legacy reader doesn't update the matrix when a transform changes"}}},
        {"new prim with a new material",
            [&](const UsdStageRefPtr& stage) {
                UsdGeomCube::Define(stage, cube).CreateSizeAttr().Set(1.0);
                BindMaterial(stage, cube,
                    DefinePreviewMaterial(stage, SdfPath("/World/materials/cube_mat"), GfVec3f(0.05f, 0.05f, 0.8f)));
            },
            ExpectColor(Color::Blue),
            {{ProceduralHydra, "a node created in procedural_update isn't rendered when no existing node was modified"},
                {ProceduralLegacyReader, legacyReaderIssue}}},
        {"prim removal", [&](const UsdStageRefPtr& stage) { stage->RemovePrim(cube); }, ExpectColor(Color::Empty),
            {{ProceduralLegacyReader, legacyReaderIssue}}},
        {"transform change, moving the sphere back",
            [&](const UsdStageRefPtr& stage) { SetTranslate(stage, sphere, GfVec3d(0, 0, 0)); },
            ExpectColor(Color::Green)},
    };
    return Run(scenario);
}
