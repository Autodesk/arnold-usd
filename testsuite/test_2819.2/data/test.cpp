// Edits of the camera projection while the host sends a framing. See framing_test_utils.h.
#include <framing_test_utils.h>

#include <pxr/usd/usdGeom/camera.h>

using namespace interactive_usd_test;

namespace {

void SetFocalLength(const UsdStageRefPtr& stage, const SdfPath& camera, float focalLength)
{
    UsdGeomCamera(stage->GetPrimAtPath(camera)).CreateFocalLengthAttr().Set(focalLength);
}

void SetAperture(const UsdStageRefPtr& stage, const SdfPath& camera, float horizontal, float vertical)
{
    UsdGeomCamera usdCamera(stage->GetPrimAtPath(camera));
    usdCamera.CreateHorizontalApertureAttr().Set(horizontal);
    usdCamera.CreateVerticalApertureAttr().Set(vertical);
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const GfRect2i full(GfVec2i(0, 0), kFramingWidth, kFramingHeight);
    // Film translate: the center of the film is at (100, 70)
    const CameraUtilFraming filmTranslate = MakeFraming(GfRange2f(GfVec2f(20, 10), GfVec2f(180, 130)), full);
    // A focal length twice as long scales the film by 2 around its center
    const CameraUtilFraming longFocal = MakeFraming(GfRange2f(GfVec2f(-60, -50), GfVec2f(260, 190)), full);
    // An aperture twice as big scales the film by 1/2 around its center
    const CameraUtilFraming bigAperture = MakeFraming(GfRange2f(GfVec2f(60, 40), GfVec2f(140, 100)), full);

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.framing = filmTranslate;
    scenario.createStage = [&]() { return CreateFramingStage(scenario.camera); };
    const SdfPath camera = scenario.camera;

    scenario.steps = {
        {"film translate framing", nullptr, ExpectSphere(filmTranslate)},
        {"focal length twice as long", [=](const UsdStageRefPtr& stage) { SetFocalLength(stage, camera, 70.f); },
            ExpectSphere(longFocal)},
        {"focal length restored", [=](const UsdStageRefPtr& stage) { SetFocalLength(stage, camera, 35.f); },
            ExpectSphere(filmTranslate)},
        {"aperture twice as big", [=](const UsdStageRefPtr& stage) { SetAperture(stage, camera, 72.f, 54.f); },
            ExpectSphere(bigAperture)},
        {"aperture restored", [=](const UsdStageRefPtr& stage) { SetAperture(stage, camera, 36.f, 27.f); },
            ExpectSphere(filmTranslate)},
    };
    return Run(scenario);
}
