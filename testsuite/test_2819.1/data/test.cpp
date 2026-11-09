// Edits of the aperture offset and of the user screen window of a camera, without host framing. The image is checked
// against the display window equivalent to the screen window, i.e. where the film (-1..1) lands in the image. See
// framing_test_utils.h.
#include <framing_test_utils.h>

#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

using namespace interactive_usd_test;

namespace {

void SetApertureOffset(const UsdStageRefPtr& stage, const SdfPath& camera, float horizontal, float vertical)
{
    UsdGeomCamera usdCamera(stage->GetPrimAtPath(camera));
    usdCamera.CreateHorizontalApertureOffsetAttr().Set(horizontal);
    usdCamera.CreateVerticalApertureOffsetAttr().Set(vertical);
}

void SetUserWindow(const UsdStageRefPtr& stage, const SdfPath& camera, const GfVec2f& min, const GfVec2f& max)
{
    UsdGeomPrimvarsAPI api(stage->GetPrimAtPath(camera));
    api.CreatePrimvar(TfToken("arnold:screen_window_min"), SdfValueTypeNames->Float2, UsdGeomTokens->constant).Set(min);
    api.CreatePrimvar(TfToken("arnold:screen_window_max"), SdfValueTypeNames->Float2, UsdGeomTokens->constant).Set(max);
}

void RemoveUserWindow(const UsdStageRefPtr& stage, const SdfPath& camera)
{
    UsdGeomPrimvarsAPI api(stage->GetPrimAtPath(camera));
    api.RemovePrimvar(TfToken("arnold:screen_window_min"));
    api.RemovePrimvar(TfToken("arnold:screen_window_max"));
}

} // namespace

int main(int, char**)
{
    const GfRect2i full(GfVec2i(0, 0), kFramingWidth, kFramingHeight);
    const CameraUtilFraming none = FullFraming();
    // An aperture offset of 9 mm on a 36 mm aperture is 0.5 in screen window units: the screen window is
    // (-0.5, 1.5) in x, so the film spans (-40, 120) pixels
    const CameraUtilFraming offset = MakeFraming(GfRange2f(GfVec2f(-40, 0), GfVec2f(120, 120)), full);
    // A user screen window of (-2, -2)/(2, 2): the film is half the size of the image
    const CameraUtilFraming userWindow = MakeFraming(GfRange2f(GfVec2f(40, 30), GfVec2f(120, 90)), full);
    // The user window moved by an offset of (18, 13.5) mm, that is (1, 1) in screen window units: (-1, -1)/(3, 3),
    // the film is (0, 60)-(80, 120) pixels
    const CameraUtilFraming userWindowOffset = MakeFraming(GfRange2f(GfVec2f(0, 60), GfVec2f(80, 120)), full);

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.createStage = [&]() { return CreateFramingStage(scenario.camera); };
    const SdfPath camera = scenario.camera;

    scenario.steps = {
        {"initial render", nullptr, ExpectSphere(none)},
        {"horizontal aperture offset", [=](const UsdStageRefPtr& stage) { SetApertureOffset(stage, camera, 9.f, 0.f); },
            ExpectSphere(offset)},
        {"aperture offset reset", [=](const UsdStageRefPtr& stage) { SetApertureOffset(stage, camera, 0.f, 0.f); },
            ExpectSphere(none)},
        {"user screen window", [=](const UsdStageRefPtr& stage) { SetUserWindow(stage, camera, GfVec2f(-2.f), GfVec2f(2.f)); },
            ExpectSphere(userWindow)},
        {"aperture offset with a user screen window",
            [=](const UsdStageRefPtr& stage) { SetApertureOffset(stage, camera, 18.f, 13.5f); },
            ExpectSphere(userWindowOffset)},
        {"aperture offset reset, the user screen window is kept",
            [=](const UsdStageRefPtr& stage) { SetApertureOffset(stage, camera, 0.f, 0.f); }, ExpectSphere(userWindow)},
        {"user screen window removed", [=](const UsdStageRefPtr& stage) { RemoveUserWindow(stage, camera); },
            ExpectSphere(none)},
    };
    return Run(scenario);
}
