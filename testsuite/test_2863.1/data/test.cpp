// The render delegate resamples the light transforms when the camera shutter changes: a sphere light moving along X is
// sharp with an instantaneous shutter, blurred once the shutter is widened, and sharp again when it's set back. See
// interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>

#include <string>

using namespace interactive_usd_test;

namespace {

const SdfPath lightPath("/World/light");

// The light has a radius of 0.5 and moves by 1 unit per frame: a point at 0.8 along X is only covered by the light
// when it's blurred over a shutter of 1 frame
std::string CheckLight(const Image& image, bool blurred)
{
    const GfVec4f center = image.Average(ScreenPosition(GfVec3d(0, 0, 0))[0], ScreenPosition(GfVec3d(0, 0, 0))[1]);
    if (center[0] < 0.5f || center[1] > 0.1f)
        return "the light isn't red at the center of the image";
    for (const double x : {-0.8, 0.8}) {
        const GfVec2f position = ScreenPosition(GfVec3d(x, 0, 0));
        const float red = image.Average(position[0], position[1], 1)[0];
        if (blurred && red < 0.05f)
            return "the light isn't blurred at x = " + std::to_string(x) + " (red " + std::to_string(red) + ")";
        if (!blurred && red > 0.02f)
            return "the light is blurred at x = " + std::to_string(x) + " (red " + std::to_string(red) + ")";
    }
    return "";
}

void SetShutter(const UsdStageRefPtr& stage, double open, double close)
{
    UsdGeomCamera camera(stage->GetPrimAtPath(SdfPath("/World/camera")));
    camera.CreateShutterOpenAttr().Set(open);
    camera.CreateShutterCloseAttr().Set(close);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    Scenario scenario;
    // The scene is evaluated at a time, and the shutter is read from the camera, by the render delegate only
    scenario.methods = RenderDelegate;
    scenario.time = 0.0;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        SetShutter(stage, 0.0, 0.0);
        UsdLuxSphereLight light = UsdLuxSphereLight::Define(stage, lightPath);
        light.CreateRadiusAttr().Set(0.5f);
        light.CreateColorAttr().Set(GfVec3f(1.f, 0.f, 0.f));
        light.CreateIntensityAttr().Set(1.f);
        light.CreateNormalizeAttr().Set(false);
        light.GetPrim().CreateAttribute(TfToken("primvars:arnold:camera"), SdfValueTypeNames->Float).Set(1.f);
        UsdGeomXformOp translate = UsdGeomXformable(light).AddTranslateOp();
        translate.Set(GfVec3d(-100, 0, 0), UsdTimeCode(-100));
        translate.Set(GfVec3d(100, 0, 0), UsdTimeCode(100));
        return stage;
    };

    scenario.steps = {
        {"instantaneous shutter", nullptr, [](const Image& image) { return CheckLight(image, false); }},
        {"shutter widened to 1 frame",
            [](const UsdStageRefPtr& stage) { SetShutter(stage, -0.5, 0.5); },
            [](const Image& image) { return CheckLight(image, true); }},
        {"instantaneous shutter again",
            [](const UsdStageRefPtr& stage) { SetShutter(stage, 0.0, 0.0); },
            [](const Image& image) { return CheckLight(image, false); }},
    };
    // The blurred render is compared against the reference image
    scenario.referenceStep = 1;
    return Run(scenario);
}
