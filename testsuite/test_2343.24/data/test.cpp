// Interactive change of the arnold type of a linked light: a sphere light linked to one of two spheres gets a spot
// shaping, turning it into an arnold spot light, which is removed, and its light link collection is then edited. An
// occluder then shadows the right sphere, until the shadow link of the spot light excludes it.
// Covers the fix of the light type change with light linking (#2663): the light link of the previous node wasn't
// deregistered, so the new node wasn't in the light groups of the shapes. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kLeft("/World/left");
const SdfPath kRight("/World/right");
const SdfPath kLight("/World/light");
const SdfPath kOccluder("/World/occluder");

UsdCollectionAPI LightLink(const UsdStageRefPtr& stage)
{
    return UsdLuxLightAPI(stage->GetPrimAtPath(kLight)).GetLightLinkCollectionAPI();
}

UsdCollectionAPI ShadowLink(const UsdStageRefPtr& stage)
{
    return UsdLuxLightAPI(stage->GetPrimAtPath(kLight)).GetShadowLinkCollectionAPI();
}

// Links to the given prims only
void SetLinks(const UsdCollectionAPI& collection, const SdfPathVector& prims)
{
    collection.CreateIncludeRootAttr().Set(false);
    collection.CreateIncludesRel().SetTargets(prims);
}

void DefineSphere(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3d& position)
{
    UsdGeomSphere::Define(stage, path).CreateRadiusAttr().Set(0.8);
    SetTranslate(stage, path, position);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const GfVec2f left = ScreenPosition(GfVec3d(-1.5, 0, 0.6));
    const GfVec2f right = ScreenPosition(GfVec3d(1.5, 0, 0.6));
    const Check onlyLeft = AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Black, right)});
    const Check both = AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Grey, right)});

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineSphere(stage, kLeft, GfVec3d(-1.5, 0, 0));
        DefineSphere(stage, kRight, GfVec3d(1.5, 0, 0));
        // The only light, in front of the spheres and pointing at them
        UsdLuxSphereLight light = UsdLuxSphereLight::Define(stage, kLight);
        light.CreateRadiusAttr().Set(0.1f);
        light.CreateIntensityAttr().Set(30.f);
        light.CreateNormalizeAttr().Set(true);
        SetTranslate(stage, kLight, GfVec3d(0, 0, 3));
        SetLinks(LightLink(stage), {kLeft});
        return stage;
    };

    scenario.steps = {
        {"initial render, the light is linked to the left sphere only", nullptr, onlyLeft},
        {"spot shaping added to the light, changing its arnold type",
            [](const UsdStageRefPtr& stage) {
                UsdLuxShapingAPI::Apply(stage->GetPrimAtPath(kLight)).CreateShapingConeAngleAttr().Set(90.f);
            },
            onlyLeft},
        {"spot shaping removed, back to a point light",
            [](const UsdStageRefPtr& stage) {
                UsdPrim light = stage->GetPrimAtPath(kLight);
                light.RemoveProperty(UsdLuxTokens->inputsShapingConeAngle);
                light.RemoveAPI<UsdLuxShapingAPI>();
            },
            onlyLeft},
        {"right sphere added to the light link collection",
            [](const UsdStageRefPtr& stage) { SetLinks(LightLink(stage), {kLeft, kRight}); }, both},
        {"occluder, invisible to the camera, between the light and the right sphere",
            [](const UsdStageRefPtr& stage) {
                // Halfway between the light and the right probe
                UsdGeomSphere::Define(stage, kOccluder).CreateRadiusAttr().Set(0.3);
                SetTranslate(stage, kOccluder, GfVec3d(0.75, 0, 1.8));
                UsdGeomPrimvarsAPI(stage->GetPrimAtPath(kOccluder))
                    .CreatePrimvar(TfToken("arnold:visibility:camera"), SdfValueTypeNames->Bool,
                        UsdGeomTokens->constant)
                    .Set(false);
            },
            AllOf({ExpectColor(Color::Grey, left), ExpectColor(Color::Black, right)})},
        {"spot shaping added again, with a shadow link to the left sphere only, the right one isn't shadowed",
            [](const UsdStageRefPtr& stage) {
                UsdLuxShapingAPI::Apply(stage->GetPrimAtPath(kLight)).CreateShapingConeAngleAttr().Set(90.f);
                SetLinks(ShadowLink(stage), {kLeft});
            },
            both},
        {"right sphere removed from the light link collection, with the spot light",
            [](const UsdStageRefPtr& stage) { SetLinks(LightLink(stage), {kLeft}); }, onlyLeft},
    };
    scenario.skippedModes = {{ProceduralLegacyReader, "this test covers the hydra code paths"}};
    return Run(scenario);
}
