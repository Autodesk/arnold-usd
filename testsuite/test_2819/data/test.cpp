// A host sending a CameraUtilFraming to the render delegate: its display window positions the film in the buffer, its
// data window is the part of the buffer that gets filled. See interactive_usd_test.h.
#include <framing_test_utils.h>

#include <pxr/usd/usdGeom/camera.h>

using namespace interactive_usd_test;

namespace {

void SetApertureOffset(const UsdStageRefPtr& stage, const SdfPath& camera, float offset)
{
    UsdGeomCamera(stage->GetPrimAtPath(camera)).CreateHorizontalApertureOffsetAttr().Set(offset);
}

} // namespace

int main(int, char**)
{
    const GfRect2i fullRect(GfVec2i(0, 0), kFramingWidth, kFramingHeight);
    const GfRange2f fullRange(GfVec2f(0, 0), GfVec2f(kFramingWidth, kFramingHeight));

    // The same as the default framing of the scenario, the display window being the data window
    const CameraUtilFraming noFraming = FullFraming();
    // Film translate: the display window is moved, the data window is the whole buffer
    const CameraUtilFraming filmTranslate = MakeFraming(GfRange2f(GfVec2f(20, 10), GfVec2f(180, 130)), fullRect);
    // Overscan: the display window is half the size of the buffer, around its center
    const CameraUtilFraming overscan = MakeFraming(GfRange2f(GfVec2f(40, 30), GfVec2f(120, 90)), fullRect);
    // Zoom: the display window is twice the size of the buffer, around its center
    const CameraUtilFraming zoom = MakeFraming(GfRange2f(GfVec2f(-80, -60), GfVec2f(240, 180)), fullRect);
    // usdview cropping to the camera viewport: the display window is the data window, both away from the origin
    // and with the camera's aspect ratio
    const CameraUtilFraming crop = MakeFraming(GfRange2f(GfVec2f(60, 30), GfVec2f(140, 90)), GfRect2i(GfVec2i(60, 30), 80, 60));
    // Render region: only the bottom right quarter of the buffer is rendered, the sphere is centered at its corner
    const CameraUtilFraming renderRegion = MakeFraming(fullRange, GfRect2i(GfVec2i(80, 60), 80, 60));

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.createStage = [&]() { return CreateFramingStage(scenario.camera); };

    // The sphere is at the center of the image, unless the camera's aperture offset moves it away from it
    const Check apertureOffset = ExpectColor(Color::Empty, 0.5f, 0.5f);

    scenario.steps = {
        {"no framing, the display window is the data window", nullptr, ExpectSphere(noFraming)},
        {"film translate", nullptr, ExpectSphere(filmTranslate), {}, filmTranslate},
        {"overscan, the film is half the size of the buffer", nullptr, ExpectSphere(overscan), {}, overscan},
        {"zoom, the film is twice the size of the buffer", nullptr, ExpectSphere(zoom), {}, zoom},
        {"display window == data window, away from the origin", nullptr, ExpectSphere(crop), {}, crop},
        {"render region, the data window is a quarter of the buffer", nullptr, ExpectSphere(renderRegion), {},
            renderRegion},
        {"back to no framing", nullptr, ExpectSphere(noFraming), {}, noFraming},
        {"camera aperture offset", [&](const UsdStageRefPtr& stage) { SetApertureOffset(stage, scenario.camera, 10.f); },
            apertureOffset},
        {"camera aperture offset reset", [&](const UsdStageRefPtr& stage) { SetApertureOffset(stage, scenario.camera, 0.f); },
            ExpectSphere(noFraming)},
    };
    return Run(scenario);
}
