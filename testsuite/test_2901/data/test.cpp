// Interactive edits of lights and cameras: a grey sphere lit by a distant light, whose color and intensity change,
// before the camera moves away from the sphere and back. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdLux/distantLight.h>

using namespace interactive_usd_test;

int main(int, char**)
{
    const SdfPath light("/World/light");

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, light);
        const SdfPath sphere("/World/sphere");
        UsdGeomSphere::Define(stage, sphere).CreateRadiusAttr().Set(1.0);
        BindMaterial(
            stage, sphere, DefinePreviewMaterial(stage, SdfPath("/World/materials/grey"), GfVec3f(0.5f, 0.5f, 0.5f)));
        return stage;
    };

    auto distantLight = [light](const UsdStageRefPtr& stage) { return UsdLuxDistantLight::Get(stage, light); };
    scenario.steps = {
        {"initial render", nullptr, ExpectColor(Color::Grey)},
        {"light color change",
            [=](const UsdStageRefPtr& stage) { distantLight(stage).GetColorAttr().Set(GfVec3f(0.f, 1.f, 0.f)); },
            ExpectColor(Color::Green)},
        {"light intensity change to 0",
            [=](const UsdStageRefPtr& stage) { distantLight(stage).GetIntensityAttr().Set(0.f); },
            ExpectColor(Color::Black)},
        {"light intensity change back",
            [=](const UsdStageRefPtr& stage) { distantLight(stage).GetIntensityAttr().Set(3.f); },
            ExpectColor(Color::Green)},
        {"camera transform change, looking away from the sphere",
            [&](const UsdStageRefPtr& stage) { SetTranslate(stage, scenario.camera, GfVec3d(10, 0, 6)); },
            ExpectColor(Color::Empty)},
        {"camera transform change back",
            [&](const UsdStageRefPtr& stage) { SetTranslate(stage, scenario.camera, GfVec3d(0, 0, 6)); },
            AllOf({ExpectColor(Color::Green), ExpectColor(Color::Empty, 0.1f, 0.1f)})},
    };
    return Run(scenario);
}
