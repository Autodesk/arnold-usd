// A host sending a CameraUtilFraming to the render delegate: its display window positions the film in the buffer, its
// data window is the part of the buffer that gets filled. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

#include <cmath>

using namespace interactive_usd_test;

namespace {

const int kWidth = 160;
const int kHeight = 120;
const double kRadius = 1.2;

CameraUtilFraming Framing(const GfRange2f& displayWindow, const GfRect2i& dataWindow)
{
    return CameraUtilFraming(displayWindow, dataWindow);
}

// Checks the pixels at a grid of world positions in the plane of the sphere center, projected with the framing like
// ApplyToProjectionMatrix does: the sphere where it is expected in the data window, the background around it, and
// nothing outside of the data window. Positions too close to an edge are skipped.
Check ExpectSphere(const CameraUtilFraming& framing)
{
    // The scale in pixels of the plane of the sphere center, and the radius of the sphere silhouette
    const double pixelsPerUnit = framing.displayWindow.GetSize()[0] / (2.0 * 6.0 * 18.0 / 35.0);
    const double silhouette = kRadius * 6.0 / std::sqrt(36.0 - kRadius * kRadius);
    const double margin = 4.0;

    std::vector<Check> checks;
    const double offsets[] = {-2.4, -1.7, -0.4, 0.0, 0.4, 1.7, 2.4};
    for (double x : offsets) {
        for (double y : offsets) {
            const GfVec2f position = FramedScreenPosition(GfVec3d(x, y, 0), framing, kWidth, kHeight);
            const double px = position[0] * kWidth;
            const double py = position[1] * kHeight;
            if (px < margin || py < margin || px > kWidth - margin || py > kHeight - margin)
                continue;
            const GfRect2i& data = framing.dataWindow;
            const bool inside = px >= data.GetMinX() + margin && py >= data.GetMinY() + margin &&
                px <= data.GetMaxX() + 1 - margin && py <= data.GetMaxY() + 1 - margin;
            const bool outside = px <= data.GetMinX() - margin || py <= data.GetMinY() - margin ||
                px >= data.GetMaxX() + 1 + margin || py >= data.GetMaxY() + 1 + margin;
            const double distance = std::sqrt(x * x + y * y);
            if (outside)
                checks.push_back(ExpectColor(Color::Empty, position));
            else if (inside && distance <= 0.5)
                checks.push_back(ExpectColor(Color::Red, position));
            else if (inside && (distance - silhouette) * pixelsPerUnit >= margin)
                checks.push_back(ExpectColor(Color::Empty, position));
        }
    }
    return AllOf(checks);
}

void SetApertureOffset(const UsdStageRefPtr& stage, const SdfPath& camera, float offset)
{
    UsdGeomCamera(stage->GetPrimAtPath(camera)).CreateHorizontalApertureOffsetAttr().Set(offset);
}

} // namespace

int main(int, char**)
{
    const SdfPath sphere("/World/sphere");
    const GfRect2i fullRect(GfVec2i(0, 0), kWidth, kHeight);
    const GfRange2f fullRange(GfVec2f(0, 0), GfVec2f(kWidth, kHeight));

    // The same as the default framing of the scenario, the display window being the data window
    const CameraUtilFraming noFraming(fullRect);
    // Film translate: the display window is moved, the data window is the whole buffer
    const CameraUtilFraming filmTranslate = Framing(GfRange2f(GfVec2f(20, 10), GfVec2f(180, 130)), fullRect);
    // Overscan: the display window is half the size of the buffer, around its center
    const CameraUtilFraming overscan = Framing(GfRange2f(GfVec2f(40, 30), GfVec2f(120, 90)), fullRect);
    // Zoom: the display window is twice the size of the buffer, around its center
    const CameraUtilFraming zoom = Framing(GfRange2f(GfVec2f(-80, -60), GfVec2f(240, 180)), fullRect);
    // usdview cropping to the camera viewport: the display window is the data window, both away from the origin
    // and with the camera's aspect ratio
    const CameraUtilFraming crop = Framing(GfRange2f(GfVec2f(60, 30), GfVec2f(140, 90)), GfRect2i(GfVec2i(60, 30), 80, 60));
    // Render region: only the bottom right quarter of the buffer is rendered, the sphere is centered at its corner
    const CameraUtilFraming renderRegion = Framing(fullRange, GfRect2i(GfVec2i(80, 60), 80, 60));

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        UsdGeomSphere::Define(stage, sphere).CreateRadiusAttr().Set(kRadius);
        BindMaterial(stage, sphere,
            DefinePreviewMaterial(stage, SdfPath("/World/materials/sphere_mat"), GfVec3f(0.8f, 0.05f, 0.05f)));
        return stage;
    };

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
