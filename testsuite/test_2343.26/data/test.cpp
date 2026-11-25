// Interactive edits of an ArnoldOptions prim: ignore_lights, enabled from the start, is disabled, enabled again and
// removed.
// Covers the fix of the options updates (#2666): the render interrupt of HdArnoldOptions::Sync was shadowed by a loop
// variable, so the edits of the options prim never restarted the render. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

// The usdImagingArnold adapter only reads an ArnoldOptions prim named options
const SdfPath kOptions("/options");

void SetIgnoreLights(const UsdStageRefPtr& stage, bool value)
{
    stage->GetPrimAtPath(kOptions).CreateAttribute(TfToken("arnold:ignore_lights"), SdfValueTypeNames->Bool).Set(value);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const std::vector<KnownIssue> editIssue = {{DelegateHydra1,
        "with hydra 1, the edits of an ArnoldOptions prim aren't applied, ArnoldOptionsAdapter::ProcessPropertyChange "
        "returns no dirty bits"}};
    const std::vector<KnownIssue> hydra2Issue = {{DelegateHydra2,
        "with hydra 2, the attributes of an ArnoldOptions prim aren't applied, even before the first render"}};
    Scenario scenario;
    // Only the render delegate reads the ArnoldOptions prims as the options of its render
    scenario.methods = RenderDelegate;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        UsdGeomSphere::Define(stage, SdfPath("/World/sphere")).CreateRadiusAttr().Set(1.0);
        stage->DefinePrim(kOptions, TfToken("ArnoldOptions"));
        SetIgnoreLights(stage, true);
        return stage;
    };

    scenario.steps = {
        {"initial render, with ignore_lights enabled on the options prim", nullptr, ExpectColor(Color::Black),
            hydra2Issue},
        {"ignore_lights disabled", [](const UsdStageRefPtr& stage) { SetIgnoreLights(stage, false); },
            ExpectColor(Color::Grey), editIssue},
        {"ignore_lights enabled again", [](const UsdStageRefPtr& stage) { SetIgnoreLights(stage, true); },
            ExpectColor(Color::Black), hydra2Issue},
        {"ignore_lights attribute removed, back to the default",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kOptions).RemoveProperty(TfToken("arnold:ignore_lights"));
            },
            ExpectColor(Color::Grey), editIssue},
    };
    return Run(scenario);
}
