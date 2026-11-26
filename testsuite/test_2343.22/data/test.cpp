// Interactive edits of an ArnoldGinstance prim instancing a cube: transform, inherit_xform, visibility and the
// instanced node. Covers the fix of the ArnoldGinstance updates (#2532): once Arnold has turned the ginstance into an
// instance of its target, the lookup of its builtin parameters returned null, which was dereferenced on the next edit.
// See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kSource("/World/source");
const SdfPath kOtherSource("/World/other_source");
const SdfPath kGinstance("/World/ginstance");

void SetArnoldBool(const UsdStageRefPtr& stage, const char* name, bool value)
{
    stage->GetPrimAtPath(kGinstance).CreateAttribute(TfToken(name), SdfValueTypeNames->Bool).Set(value);
}

void SetVisibility(const UsdStageRefPtr& stage, unsigned char value)
{
    stage->GetPrimAtPath(kGinstance)
        .CreateAttribute(TfToken("arnold:visibility"), SdfValueTypeNames->UChar)
        .Set(value);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const std::vector<KnownIssue> morphedGinstanceIssue = {{AnyMode,
        "once Arnold has turned the ginstance into an instance of its target, the lookup of its node and inherit_xform "
        "parameters returns null in HdArnoldNativeRprim::Sync, so their edits are ignored (#2532 only avoids the "
        "crash)"}};
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        // The source cube is above the frame, only its ginstance is seen
        UsdGeomCube::Define(stage, kSource).CreateSizeAttr().Set(1.0);
        SetTranslate(stage, kSource, GfVec3d(0, 10, 0));
        BindMaterial(stage, kSource, DefinePreviewMaterial(stage, SdfPath("/World/materials/red"),
            GfVec3f(0.8f, 0.05f, 0.05f)));
        UsdGeomCube::Define(stage, kOtherSource).CreateSizeAttr().Set(1.0);
        SetTranslate(stage, kOtherSource, GfVec3d(0, -10, 0));
        BindMaterial(stage, kOtherSource, DefinePreviewMaterial(stage, SdfPath("/World/materials/green"),
            GfVec3f(0.05f, 0.8f, 0.05f)));
        UsdPrim ginstance = stage->DefinePrim(kGinstance, TfToken("ArnoldGinstance"));
        ginstance.CreateAttribute(TfToken("arnold:node"), SdfValueTypeNames->String).Set(kSource.GetString());
        SetArnoldBool(stage, "arnold:inherit_xform", false);
        SetTranslate(stage, kGinstance, GfVec3d(0, 0, 0));
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, ExpectColor(Color::Red, center)},
        {"ginstance moved to the right",
            [](const UsdStageRefPtr& stage) { SetTranslate(stage, kGinstance, GfVec3d(1.5, 0, 0)); },
            AllOf({ExpectColor(Color::Empty, center), ExpectColor(Color::Red, right)})},
        {"inherit_xform enabled, adding the transform of the source and moving the ginstance above the frame",
            [](const UsdStageRefPtr& stage) { SetArnoldBool(stage, "arnold:inherit_xform", true); },
            AllOf({ExpectColor(Color::Empty, center), ExpectColor(Color::Empty, right)}), morphedGinstanceIssue},
        {"inherit_xform disabled",
            [](const UsdStageRefPtr& stage) { SetArnoldBool(stage, "arnold:inherit_xform", false); },
            ExpectColor(Color::Red, right)},
        {"visibility set to 0", [](const UsdStageRefPtr& stage) { SetVisibility(stage, 0); },
            ExpectColor(Color::Empty, right)},
        {"visibility restored", [](const UsdStageRefPtr& stage) { SetVisibility(stage, 255); },
            ExpectColor(Color::Red, right)},
        {"instanced node changed to a green cube",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kGinstance).GetAttribute(TfToken("arnold:node")).Set(kOtherSource.GetString());
            },
            ExpectColor(Color::Green, right), morphedGinstanceIssue},
    };
    // The last step is a known issue in every mode, the reference image is the render before it
    scenario.referenceStep = 5;
    scenario.skippedModes = {{ProceduralLegacyReader, "this test covers the hydra code paths, the legacy reader "
                                                      "ignores transform changes"}};
    return Run(scenario);
}
