// Interactive edits of the per-instance primvars of a point instancer, with HDARNOLD_GEOM_DEDUP=0: the prototype isn't
// deduplicated, so it's instanced with the instance_matrix of its own shape node, which keeps the instance primvars
// declared on it between the edits. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/base/tf/setenv.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kInstancer("/World/instancer");
const SdfPath kCube("/World/instancer/prototypes/cube");

UsdGeomPrimvar InstanceColors(const UsdStageRefPtr& stage)
{
    return UsdGeomPrimvarsAPI(stage->GetPrimAtPath(kInstancer))
        .CreatePrimvar(UsdGeomTokens->primvarsDisplayColor, SdfValueTypeNames->Color3fArray, UsdGeomTokens->vertex);
}

} // namespace

int main(int, char**)
{
    // Read when the render delegate is created
    TfSetenv("HDARNOLD_GEOM_DEDUP", "0");

    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0));
    const VtVec3fArray greenAndBlue = {GfVec3f(0.05f, 0.8f, 0.05f), GfVec3f(0.05f, 0.05f, 0.8f)};
    const VtVec3fArray blueAndGreen = {GfVec3f(0.05f, 0.05f, 0.8f), GfVec3f(0.05f, 0.8f, 0.05f)};

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));

        // The prototype has no displayColor nor material, it's rendered with the fallback shader, white unless an
        // instance displayColor is set
        UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, kInstancer);
        UsdGeomCube::Define(stage, kCube).CreateSizeAttr().Set(0.8);
        instancer.CreatePrototypesRel().SetTargets({kCube});
        instancer.CreatePositionsAttr().Set(VtVec3fArray{GfVec3f(-1.5f, 0.f, 0.f), GfVec3f(1.5f, 0.f, 0.f)});
        instancer.CreateProtoIndicesAttr().Set(VtIntArray{0, 0});
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Grey, right)})},
        {"per-instance displayColor primvar added",
            [=](const UsdStageRefPtr& stage) { InstanceColors(stage).Set(greenAndBlue); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Blue, right)})},
        {"per-instance displayColor primvar change",
            [=](const UsdStageRefPtr& stage) { InstanceColors(stage).Set(blueAndGreen); },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Green, right)})},
        {"per-instance displayColor primvar blocked",
            [](const UsdStageRefPtr& stage) { InstanceColors(stage).GetAttr().Block(); },
            AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Grey, right)})},
        {"per-instance displayColor primvar set again",
            [=](const UsdStageRefPtr& stage) { InstanceColors(stage).Set(greenAndBlue); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Blue, right)})},
        {"per-instance displayColor primvar removed",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kInstancer).RemoveProperty(UsdGeomTokens->primvarsDisplayColor);
            },
            AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Grey, right)})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "this test covers the hydra code paths"}};
    return Run(scenario);
}
