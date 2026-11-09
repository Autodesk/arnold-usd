// Interactive edits rebuilding prims: variant selection switches, prim deactivation and reactivation, a prim type
// change, the removal of a parent prim, a prim added and removed before the render, and a prim renamed and moved.
// See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/namespaceEdit.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kAsset("/World/asset");
const SdfPath kAssetGeo("/World/asset/geo");
const SdfPath kLeft("/World/left");
const SdfPath kMoved("/World/moved");
const SdfPath kGroup("/World/group");
const GfVec3f kRed(0.8f, 0.05f, 0.05f);
const GfVec3f kGreen(0.05f, 0.8f, 0.05f);
const GfVec3f kBlue(0.05f, 0.05f, 0.8f);

void SetDisplayColor(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3f& color)
{
    UsdGeomGprim(stage->GetPrimAtPath(path)).CreateDisplayColorPrimvar().Set(VtVec3fArray{color});
}

void DefineSphere(const UsdStageRefPtr& stage, const SdfPath& path, double radius, const GfVec3f& color)
{
    UsdGeomSphere::Define(stage, path).CreateRadiusAttr().Set(radius);
    SetDisplayColor(stage, path, color);
}

UsdVariantSet ShapeVariantSet(const UsdStageRefPtr& stage)
{
    return stage->GetPrimAtPath(kAsset).GetVariantSets().GetVariantSet("shape");
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const std::vector<KnownIssue> variantIssue = {{ProceduralHydra2,
        "the hydra 2 procedural renders the sphere much darker after switching the variant back to it"}};

    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    // Inside the cube variant, outside the sphere variant
    const GfVec2f cubeCorner = ScreenPosition(GfVec3d(0.7, 0.7, 0));
    const GfVec2f left = ScreenPosition(GfVec3d(-2, 0, 0));
    // Inside a cube of size 1.2 centered on the left prim, outside a sphere of radius 0.6
    const GfVec2f leftCorner = ScreenPosition(GfVec3d(-1.5, 0.5, 0));
    const GfVec2f groupTop = ScreenPosition(GfVec3d(2, 0.8, 0));
    const GfVec2f groupBottom = ScreenPosition(GfVec3d(2, -0.8, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(2, 0, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));

        // An asset in the center, whose "shape" variant set selects a red sphere or a green cube
        UsdPrim asset = UsdGeomXform::Define(stage, kAsset).GetPrim();
        UsdVariantSet variantSet = asset.GetVariantSets().AddVariantSet("shape");
        variantSet.AddVariant("sphere");
        variantSet.AddVariant("cube");
        variantSet.SetVariantSelection("sphere");
        {
            UsdEditContext context(variantSet.GetVariantEditContext());
            DefineSphere(stage, kAssetGeo, 0.8, kRed);
        }
        variantSet.SetVariantSelection("cube");
        {
            UsdEditContext context(variantSet.GetVariantEditContext());
            UsdGeomCube::Define(stage, kAssetGeo).CreateSizeAttr().Set(1.6);
            SetDisplayColor(stage, kAssetGeo, kGreen);
        }
        variantSet.SetVariantSelection("sphere");

        // A blue sphere on the left, whose type changes
        DefineSphere(stage, kLeft, 0.6, kBlue);
        SetTranslate(stage, kLeft, GfVec3d(-2, 0, 0));
        UsdGeomCube(stage->GetPrimAtPath(kLeft)).CreateSizeAttr().Set(1.2);

        // A group of two red spheres on the right
        UsdGeomXform::Define(stage, kGroup);
        SetTranslate(stage, kGroup, GfVec3d(2, 0, 0));
        for (const auto& child : {std::make_pair("top", 0.8), std::make_pair("bottom", -0.8)}) {
            const SdfPath path = kGroup.AppendChild(TfToken(child.first));
            DefineSphere(stage, path, 0.5, kRed);
            SetTranslate(stage, path, GfVec3d(0, child.second, 0));
        }
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr,
            AllOf({ExpectColor(Color::Red, center), ExpectColor(Color::Empty, cubeCorner), ExpectColor(Color::Blue, left),
                ExpectColor(Color::Empty, leftCorner), ExpectColor(Color::Red, groupTop),
                ExpectColor(Color::Red, groupBottom)})},
        {"variant selection switched to the cube",
            [](const UsdStageRefPtr& stage) { ShapeVariantSet(stage).SetVariantSelection("cube"); },
            AllOf({ExpectColor(Color::Green, center), ExpectColor(Color::Green, cubeCorner)})},
        {"variant selection switched back to the sphere",
            [](const UsdStageRefPtr& stage) { ShapeVariantSet(stage).SetVariantSelection("sphere"); },
            AllOf({ExpectColor(Color::Red, center), ExpectColor(Color::Empty, cubeCorner)}), variantIssue},
        {"asset deactivation",
            [](const UsdStageRefPtr& stage) { stage->GetPrimAtPath(kAsset).SetActive(false); },
            ExpectColor(Color::Empty, center)},
        {"asset reactivation", [](const UsdStageRefPtr& stage) { stage->GetPrimAtPath(kAsset).SetActive(true); },
            AllOf({ExpectColor(Color::Red, center), ExpectColor(Color::Empty, cubeCorner)})},
        {"type change of the left prim, from a sphere to a cube",
            [](const UsdStageRefPtr& stage) { stage->GetPrimAtPath(kLeft).SetTypeName(TfToken("Cube")); },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Blue, leftCorner)})},
        {"removal of the group with its children",
            [](const UsdStageRefPtr& stage) { stage->RemovePrim(kGroup); },
            AllOf({ExpectColor(Color::Empty, groupTop), ExpectColor(Color::Empty, groupBottom)})},
        {"a big sphere added and removed before the render",
            [](const UsdStageRefPtr& stage) {
                const SdfPath temporary("/World/temporary");
                DefineSphere(stage, temporary, 3.0, kGreen);
                stage->RemovePrim(temporary);
            },
            AllOf({ExpectColor(Color::Red, center), ExpectColor(Color::Empty, cubeCorner),
                ExpectColor(Color::Blue, left)})},
        {"left prim renamed and moved to the right",
            [](const UsdStageRefPtr& stage) {
                SdfBatchNamespaceEdit edit;
                edit.Add(SdfNamespaceEdit::Rename(kLeft, TfToken(kMoved.GetName())));
                stage->GetRootLayer()->Apply(edit);
                SetTranslate(stage, kMoved, GfVec3d(2, 0, 0));
            },
            AllOf({ExpectColor(Color::Empty, left), ExpectColor(Color::Blue, right)})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "this test covers the hydra code paths"}};
    return Run(scenario);
}
