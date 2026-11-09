// More interactive edits of a point instancer: prototype geometry, instance scales, prototypes added and removed from
// the prototypes relationship, the instancer visibility, light linking to the instancer, and per-instance primvars. The
// per-instance primvar, that might not be removed, comes last, not to change the results of the other steps.
// See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdLux/lightAPI.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kInstancer("/World/instancer");
const SdfPath kCube("/World/instancer/prototypes/cube");
const SdfPath kSphere("/World/instancer/prototypes/sphere");
const SdfPath kLight("/World/light");

UsdGeomPointInstancer Instancer(const UsdStageRefPtr& stage)
{
    return UsdGeomPointInstancer(stage->GetPrimAtPath(kInstancer));
}

UsdGeomPrimvar InstanceColors(const UsdStageRefPtr& stage)
{
    return UsdGeomPrimvarsAPI(stage->GetPrimAtPath(kInstancer))
        .CreatePrimvar(UsdGeomTokens->primvarsDisplayColor, SdfValueTypeNames->Color3fArray, UsdGeomTokens->vertex);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const std::vector<KnownIssue> scalesRemovalIssue = {{ProceduralHydra | AnyDelegate,
        "the instances keep the scales of the removed scales attribute"}};
    const std::vector<KnownIssue> scalesKeptIssue = {{ProceduralHydra2 | DelegateHydra2,
        "with hydra 2, the instances keep the removed scales until the end"}};
    const std::vector<KnownIssue> newPrototypeIssue = {{ProceduralHydra1 | DelegateHydra1,
        "with hydra 1, a prototype added to the point instancer prototypes isn't rendered"}};
    const std::vector<KnownIssue> lightLinkIssue = {{ProceduralHydra | AnyDelegate,
        "excluding the point instancer from a light link collection doesn't unlink its instances"}};
    const std::vector<KnownIssue> primvarRemovalIssue = {{ProceduralHydra | AnyDelegate,
        "the instances keep the values of a removed per-instance primvar"}};

    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0));
    // Inside a cube of size 0.8 or a sphere of radius 0.6 centered on the instances, outside a cube of size 0.3
    const GfVec2f leftEdge = ScreenPosition(GfVec3d(-1.2, 0.3, 0));
    const GfVec2f rightEdge = ScreenPosition(GfVec3d(1.8, 0.3, 0));
    // Outside a sphere of radius 0.6 centered on the right instance, inside one scaled 4 times
    const GfVec2f rightFar = ScreenPosition(GfVec3d(2.7, 0, 0));
    const VtVec3fArray greenAndBlue = {GfVec3f(0.05f, 0.8f, 0.05f), GfVec3f(0.05f, 0.05f, 0.8f)};
    const VtVec3fArray blueAndGreen = {GfVec3f(0.05f, 0.05f, 0.8f), GfVec3f(0.05f, 0.8f, 0.05f)};

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, kLight);

        // The prototypes have no displayColor nor material, they're rendered with the fallback shader, white unless
        // an instance displayColor is set
        UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, kInstancer);
        UsdGeomCube::Define(stage, kCube).CreateSizeAttr().Set(0.8);
        UsdGeomSphere::Define(stage, kSphere).CreateRadiusAttr().Set(0.6);
        instancer.CreatePrototypesRel().SetTargets({kCube});
        instancer.CreatePositionsAttr().Set(VtVec3fArray{GfVec3f(-1.5f, 0.f, 0.f), GfVec3f(1.5f, 0.f, 0.f)});
        instancer.CreateProtoIndicesAttr().Set(VtIntArray{0, 0});
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, AllOf({ExpectColor(Color::Grey, leftEdge), ExpectColor(Color::Grey, rightEdge)})},
        {"prototype size change, making the instances smaller",
            [](const UsdStageRefPtr& stage) { UsdGeomCube(stage->GetPrimAtPath(kCube)).GetSizeAttr().Set(0.3); },
            AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Empty, leftEdge), ExpectColor(Color::Grey, right),
                ExpectColor(Color::Empty, rightEdge)})},
        {"scales change, making the right instance 4 times bigger",
            [](const UsdStageRefPtr& stage) {
                Instancer(stage).CreateScalesAttr().Set(VtVec3fArray{GfVec3f(1.f), GfVec3f(4.f)});
            },
            AllOf({ExpectColor(Color::Empty, leftEdge), ExpectColor(Color::Grey, rightEdge)})},
        {"scales removed",
            [](const UsdStageRefPtr& stage) { stage->GetPrimAtPath(kInstancer).RemoveProperty(UsdGeomTokens->scales); },
            AllOf({ExpectColor(Color::Grey, right), ExpectColor(Color::Empty, rightEdge)}), scalesRemovalIssue},
        {"sphere prototype added to the prototypes, and instanced on the right",
            [](const UsdStageRefPtr& stage) {
                Instancer(stage).GetPrototypesRel().SetTargets({kCube, kSphere});
                Instancer(stage).GetProtoIndicesAttr().Set(VtIntArray{0, 1});
            },
            AllOf({ExpectColor(Color::Empty, leftEdge), ExpectColor(Color::Grey, rightEdge)}), newPrototypeIssue},
        {"cube prototype removed from the prototypes, the sphere being instanced on both sides",
            [](const UsdStageRefPtr& stage) {
                Instancer(stage).GetPrototypesRel().SetTargets({kSphere});
                Instancer(stage).GetProtoIndicesAttr().Set(VtIntArray{0, 0});
            },
            AllOf({ExpectColor(Color::Grey, leftEdge), ExpectColor(Color::Grey, rightEdge)}), newPrototypeIssue},
        {"instancer visibility change to invisible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kInstancer)).MakeInvisible(); },
            AllOf({ExpectColor(Color::Empty, left), ExpectColor(Color::Empty, right)})},
        {"instancer visibility change back to visible",
            [](const UsdStageRefPtr& stage) { UsdGeomImageable(stage->GetPrimAtPath(kInstancer)).MakeVisible(); },
            AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Grey, right)})},
        {"the instancer is excluded from the light link collection of the light",
            [](const UsdStageRefPtr& stage) {
                UsdCollectionAPI collection = UsdLuxLightAPI(stage->GetPrimAtPath(kLight)).GetLightLinkCollectionAPI();
                collection.CreateIncludeRootAttr().Set(true);
                collection.CreateExcludesRel().SetTargets({kInstancer});
            },
            AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Black, right)}), lightLinkIssue},
        {"the instancer is included again in the light link collection",
            [](const UsdStageRefPtr& stage) {
                UsdLuxLightAPI(stage->GetPrimAtPath(kLight)).GetLightLinkCollectionAPI().GetExcludesRel().ClearTargets(
                    true);
            },
            AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Grey, right), ExpectColor(Color::Empty, rightFar)}),
            scalesKeptIssue},
        {"per-instance displayColor primvar added",
            [=](const UsdStageRefPtr& stage) { InstanceColors(stage).Set(greenAndBlue); },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Blue, right)})},
        {"per-instance displayColor primvar change",
            [=](const UsdStageRefPtr& stage) { InstanceColors(stage).Set(blueAndGreen); },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Green, right)})},
        {"per-instance displayColor primvar removed",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kInstancer).RemoveProperty(UsdGeomTokens->primvarsDisplayColor);
            },
            AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Grey, right)}), primvarRemovalIssue},
    };
    // the last steps have known issues in every mode, the reference image is the render after the light
    // linking edits
    scenario.referenceStep = 9;
    scenario.skippedModes = {{ProceduralLegacyReader, "this test covers the hydra code paths"}};
    return Run(scenario);
}
