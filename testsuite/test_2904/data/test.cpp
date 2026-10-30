// Interactive edits of a prim hierarchy and of native instances: transform and visibility inherited from a parent,
// purpose, edits of the instances prototype, and an instance made editable. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kGroup("/World/group");
const SdfPath kSphere("/World/group/sphere");
const SdfPath kInstanceA("/World/instances/a");
const SdfPath kInstanceB("/World/instances/b");

void SetDisplayColor(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3f& color)
{
    UsdGeomGprim(stage->GetPrimAtPath(path)).CreateDisplayColorPrimvar().Set(VtVec3fArray{color});
}

} // namespace

int main(int, char**)
{
    const std::vector<KnownIssue> transformIssue = {
        {ProceduralLegacyReader, "the legacy reader doesn't update the matrix when a transform changes"}};
    const std::vector<KnownIssue> previousTransformIssue = {{ProceduralLegacyReader,
        "the legacy reader didn't move the sphere to the center (parent transform change step)"}};
    const std::vector<KnownIssue> prototypeIssue = {{ProceduralLegacyReader,
        "the legacy reader doesn't update native instances when their prototype changes, sometimes renders the edited "
        "prototype class itself, and renders an instance made not instanceable with mixed colors"}};
    const GfVec3f red(0.8f, 0.05f, 0.05f);
    const GfVec3f green(0.05f, 0.8f, 0.05f);
    const GfVec3f blue(0.05f, 0.05f, 0.8f);
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));
    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f instanceA = ScreenPosition(GfVec3d(1.5, 1, 0));
    const GfVec2f instanceB = ScreenPosition(GfVec3d(1.5, -1, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));

        // A red sphere under a transformed group
        UsdGeomXform::Define(stage, kGroup);
        SetTranslate(stage, kGroup, GfVec3d(-1.5, 0, 0));
        UsdGeomSphere::Define(stage, kSphere).CreateRadiusAttr().Set(0.7);
        SetDisplayColor(stage, kSphere, red);

        // Two native instances of a green cube, the prototype being a class so that it isn't rendered itself
        UsdPrim prototype = stage->CreateClassPrim(SdfPath("/Prototype"));
        UsdGeomCube::Define(stage, prototype.GetPath().AppendChild(TfToken("cube"))).CreateSizeAttr().Set(0.8);
        SetDisplayColor(stage, prototype.GetPath().AppendChild(TfToken("cube")), green);
        UsdGeomXform::Define(stage, SdfPath("/World/instances"));
        for (const auto& instance : {std::make_pair(kInstanceA, 1.0), std::make_pair(kInstanceB, -1.0)}) {
            UsdPrim prim = UsdGeomXform::Define(stage, instance.first).GetPrim();
            prim.GetReferences().AddInternalReference(prototype.GetPath());
            prim.SetInstanceable(true);
            SetTranslate(stage, instance.first, GfVec3d(1.5, instance.second, 0));
        }
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr,
            AllOf({ExpectColor(Color::Red, left), ExpectColor(Color::Green, instanceA),
                ExpectColor(Color::Green, instanceB)})},
        {"parent transform change, moving the sphere to the center",
            [](const UsdStageRefPtr& stage) { SetTranslate(stage, kGroup, GfVec3d(0, 0, 0)); },
            AllOf({ExpectColor(Color::Empty, left), ExpectColor(Color::Red, center)}), transformIssue},
        {"parent visibility change to invisible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kGroup)).MakeInvisible(); },
            ExpectColor(Color::Empty, center)},
        {"parent visibility change back to visible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kGroup)).MakeVisible(); },
            ExpectColor(Color::Red, center), previousTransformIssue},
        {"purpose change to proxy, which isn't rendered",
            [](const UsdStageRefPtr& stage) {
                UsdGeomImageable(stage->GetPrimAtPath(kSphere)).CreatePurposeAttr().Set(UsdGeomTokens->proxy);
            },
            ExpectColor(Color::Empty, center)},
        {"purpose change back to default",
            [](const UsdStageRefPtr& stage) {
                UsdGeomImageable(stage->GetPrimAtPath(kSphere)).GetPurposeAttr().Set(UsdGeomTokens->default_);
            },
            ExpectColor(Color::Red, center), previousTransformIssue},
        {"prototype displayColor change, seen by all the instances",
            [&](const UsdStageRefPtr& stage) { SetDisplayColor(stage, SdfPath("/Prototype/cube"), blue); },
            AllOf({ExpectColor(Color::Blue, instanceA), ExpectColor(Color::Blue, instanceB)}), prototypeIssue},
        {"instance made not instanceable",
            [](const UsdStageRefPtr& stage) { stage->GetPrimAtPath(kInstanceB).SetInstanceable(false); },
            AllOf({ExpectColor(Color::Blue, instanceA), ExpectColor(Color::Blue, instanceB)}), prototypeIssue},
        {"displayColor change on the prim that isn't instanced anymore",
            [&](const UsdStageRefPtr& stage) { SetDisplayColor(stage, kInstanceB.AppendChild(TfToken("cube")), red); },
            AllOf({ExpectColor(Color::Blue, instanceA), ExpectColor(Color::Red, instanceB)}), prototypeIssue},
    };
    // From the prototype edit, the legacy reader renders the stage differently from one run to the other, see
    // prototypeIssue
    scenario.referenceStep = 5;
    return Run(scenario);
}
