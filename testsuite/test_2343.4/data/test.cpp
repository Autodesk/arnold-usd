// Interactive edits of a point instancer: instance positions, prototype indices, a prototype material, invisible
// instances and the instancer transform. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/shader.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kInstancer("/World/instancer");

UsdGeomPointInstancer Instancer(const UsdStageRefPtr& stage)
{
    return UsdGeomPointInstancer(stage->GetPrimAtPath(kInstancer));
}

} // namespace

int main(int, char**)
{
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));

        // The prototypes are at the origin, where nothing is expected until an instance is added there
        const SdfPath cube = kInstancer.AppendPath(SdfPath("prototypes/cube"));
        const SdfPath sphere = kInstancer.AppendPath(SdfPath("prototypes/sphere"));
        UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, kInstancer);
        UsdGeomCube::Define(stage, cube).CreateSizeAttr().Set(1.0);
        UsdGeomSphere::Define(stage, sphere).CreateRadiusAttr().Set(0.5);
        BindMaterial(stage, cube,
            DefinePreviewMaterial(stage, SdfPath("/World/materials/cube_mat"), GfVec3f(0.8f, 0.05f, 0.05f)));
        BindMaterial(stage, sphere,
            DefinePreviewMaterial(stage, SdfPath("/World/materials/sphere_mat"), GfVec3f(0.05f, 0.8f, 0.05f)));
        instancer.CreatePrototypesRel().SetTargets({cube, sphere});
        instancer.CreatePositionsAttr().Set(VtVec3fArray{GfVec3f(-1.5f, 0.f, 0.f), GfVec3f(1.5f, 0.f, 0.f)});
        instancer.CreateProtoIndicesAttr().Set(VtIntArray{0, 0});
        SetTranslate(stage, kInstancer, GfVec3d(0, 0, 0));
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr,
            AllOf({ExpectColor(Color::Red, left), ExpectColor(Color::Empty, center), ExpectColor(Color::Red, right)})},
        {"positions change, adding an instance in the center",
            [](const UsdStageRefPtr& stage) {
                Instancer(stage).GetPositionsAttr().Set(
                    VtVec3fArray{GfVec3f(-1.5f, 0.f, 0.f), GfVec3f(0.f, 0.f, 0.f), GfVec3f(1.5f, 0.f, 0.f)});
                Instancer(stage).GetProtoIndicesAttr().Set(VtIntArray{0, 0, 0});
            },
            AllOf({ExpectColor(Color::Red, left), ExpectColor(Color::Red, center), ExpectColor(Color::Red, right)})},
        {"prototype index change, instancing the sphere in the center",
            [](const UsdStageRefPtr& stage) { Instancer(stage).GetProtoIndicesAttr().Set(VtIntArray{0, 1, 0}); },
            AllOf({ExpectColor(Color::Red, left), ExpectColor(Color::Green, center), ExpectColor(Color::Red, right)})},
        {"prototype material change",
            [](const UsdStageRefPtr& stage) {
                UsdShadeShader::Get(stage, SdfPath("/World/materials/cube_mat/surface"))
                    .GetInput(TfToken("diffuseColor"))
                    .Set(GfVec3f(0.05f, 0.05f, 0.8f));
            },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Green, center), ExpectColor(Color::Blue, right)})},
        {"invisible instance",
            [](const UsdStageRefPtr& stage) { Instancer(stage).CreateInvisibleIdsAttr().Set(VtInt64Array{0}); },
            AllOf({ExpectColor(Color::Empty, left), ExpectColor(Color::Green, center), ExpectColor(Color::Blue, right)})},
        {"instancer transform change, moving all instances out of the frame",
            [](const UsdStageRefPtr& stage) { SetTranslate(stage, kInstancer, GfVec3d(0, 10, 0)); },
            AllOf({ExpectColor(Color::Empty, center), ExpectColor(Color::Empty, right)}),
            {{ProceduralLegacyReader, "the legacy reader doesn't update the matrix when a transform changes"}}},
        {"instancer transform change back",
            [](const UsdStageRefPtr& stage) { SetTranslate(stage, kInstancer, GfVec3d(0, 0, 0)); },
            AllOf({ExpectColor(Color::Empty, left), ExpectColor(Color::Green, center), ExpectColor(Color::Blue, right)})},
    };
    return Run(scenario);
}
