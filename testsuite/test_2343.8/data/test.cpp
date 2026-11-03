// Interactive edits of light linking: the light link collections of lights are edited, a linked light is added and
// another one removed, and a prim is added and then included in a collection. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/lightAPI.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kLeft("/World/left");
const SdfPath kRight("/World/right");
const SdfPath kCenter("/World/center");
const SdfPath kRedLight("/World/red_light");
const SdfPath kGreenLight("/World/green_light");
const SdfPath kBlueLight("/World/blue_light");

UsdCollectionAPI LightLink(const UsdStageRefPtr& stage, const SdfPath& light)
{
    return UsdLuxLightAPI(stage->GetPrimAtPath(light)).GetLightLinkCollectionAPI();
}

// Links the light to the given prims only, or to everything but them
void SetLinks(const UsdStageRefPtr& stage, const SdfPath& light, const SdfPathVector& prims, bool include)
{
    UsdCollectionAPI collection = LightLink(stage, light);
    collection.CreateIncludeRootAttr().Set(!include);
    collection.CreateIncludesRel().SetTargets(include ? prims : SdfPathVector());
    collection.CreateExcludesRel().SetTargets(include ? SdfPathVector() : prims);
}

void DefineColoredLight(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3f& color)
{
    DefineDistantLight(stage, path);
    UsdLuxDistantLight::Get(stage, path).CreateColorAttr().Set(color);
}

void DefineSphere(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3d& position)
{
    UsdGeomSphere::Define(stage, path).CreateRadiusAttr().Set(0.8);
    SetTranslate(stage, path, position);
    BindMaterial(stage, path, UsdShadeMaterial::Get(stage, SdfPath("/World/materials/grey")));
}

} // namespace

int main(int, char**)
{
    const GfVec2f left = ScreenPosition(GfVec3d(-1.8, 0, 0));
    const GfVec2f right = ScreenPosition(GfVec3d(1.8, 0, 0));
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefinePreviewMaterial(stage, SdfPath("/World/materials/grey"), GfVec3f(0.5f, 0.5f, 0.5f));
        DefineSphere(stage, kLeft, GfVec3d(-1.8, 0, 0));
        DefineSphere(stage, kRight, GfVec3d(1.8, 0, 0));
        // The red light only lights the left sphere, and the green one only the right sphere
        DefineColoredLight(stage, kRedLight, GfVec3f(1.f, 0.f, 0.f));
        DefineColoredLight(stage, kGreenLight, GfVec3f(0.f, 1.f, 0.f));
        SetLinks(stage, kRedLight, {kRight}, false);
        SetLinks(stage, kGreenLight, {kLeft}, false);
        return stage;
    };

    scenario.steps = {
        {"initial render", nullptr, AllOf({ExpectColor(Color::Red, left), ExpectColor(Color::Green, right)})},
        {"light link excludes change, the red light lighting none of the spheres",
            [](const UsdStageRefPtr& stage) { SetLinks(stage, kRedLight, {kLeft, kRight}, false); },
            AllOf({ExpectColor(Color::Black, left), ExpectColor(Color::Green, right)})},
        {"light link includes, swapping the spheres lit by each light",
            [](const UsdStageRefPtr& stage) {
                SetLinks(stage, kRedLight, {kRight}, true);
                SetLinks(stage, kGreenLight, {kLeft}, true);
            },
            AllOf({ExpectColor(Color::Green, left), ExpectColor(Color::Red, right)})},
        {"new blue light only lighting the left sphere, and the green light turned off",
            [](const UsdStageRefPtr& stage) {
                DefineColoredLight(stage, kBlueLight, GfVec3f(0.f, 0.f, 1.f));
                SetLinks(stage, kBlueLight, {kLeft}, true);
                UsdLuxDistantLight::Get(stage, kGreenLight).GetIntensityAttr().Set(0.f);
            },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Red, right)})},
        {"the red light, only lighting the right sphere, is removed",
            [](const UsdStageRefPtr& stage) { stage->RemovePrim(kRedLight); },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Black, right)})},
        {"new sphere in the center, that no light is linked to",
            [](const UsdStageRefPtr& stage) { DefineSphere(stage, kCenter, GfVec3d(0, 0, 0)); },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Black, center), ExpectColor(Color::Black, right)})},
        {"the new sphere is added to the blue light includes",
            [](const UsdStageRefPtr& stage) { SetLinks(stage, kBlueLight, {kLeft, kCenter}, true); },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Blue, center), ExpectColor(Color::Black, right)})},
        {"light linking removed from the blue light, which lights everything",
            [](const UsdStageRefPtr& stage) {
                UsdCollectionAPI collection = LightLink(stage, kBlueLight);
                collection.GetIncludeRootAttr().Clear();
                collection.GetIncludesRel().ClearTargets(true);
            },
            AllOf({ExpectColor(Color::Blue, left), ExpectColor(Color::Blue, center), ExpectColor(Color::Blue, right)})},
    };
    scenario.skippedModes = {{ProceduralLegacyReader,
        "this test covers the hydra code paths, the legacy reader hangs when the red light is removed"},
        {ProceduralHydra,
        "Arnold crashes when the procedural destroys nodes in procedural_update (likely the ProceduralRemoveNode "
        "dangling node issue), at the red light removal"}};
    return Run(scenario);
}
