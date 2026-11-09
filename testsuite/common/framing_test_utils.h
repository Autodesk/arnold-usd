// Helpers shared by the interactive tests of the host CameraUtilFraming (test_2819*), see interactive_usd_test.h
#pragma once

#include "interactive_usd_test.h"

#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace interactive_usd_test {

constexpr int kFramingWidth = 160;
constexpr int kFramingHeight = 120;
constexpr double kFramingSphereRadius = 1.2;

inline CameraUtilFraming MakeFraming(const GfRange2f& displayWindow, const GfRect2i& dataWindow)
{
    return CameraUtilFraming(displayWindow, dataWindow);
}

// A framing whose display window and data window are both the full buffer
inline CameraUtilFraming FullFraming(int width = kFramingWidth, int height = kFramingHeight)
{
    return CameraUtilFraming(GfRect2i(GfVec2i(0, 0), width, height));
}

// A red sphere at the origin, lit by a distant light, seen by a camera made by DefineCamera
inline UsdStageRefPtr CreateFramingStage(const SdfPath& camera)
{
    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    UsdGeomXform::Define(stage, SdfPath("/World"));
    DefineCamera(stage, camera, GfVec3d(0, 0, 6));
    DefineDistantLight(stage, SdfPath("/World/light"));
    const SdfPath sphere("/World/sphere");
    UsdGeomSphere::Define(stage, sphere).CreateRadiusAttr().Set(kFramingSphereRadius);
    BindMaterial(stage, sphere,
        DefinePreviewMaterial(stage, SdfPath("/World/materials/sphere_mat"), GfVec3f(0.8f, 0.05f, 0.05f)));
    return stage;
}

// Checks the pixels at a grid of world positions in the plane of the sphere center, projected to normalized buffer
// positions by project: the sphere where it is expected in the data window, the background around it, and nothing
// outside of the data window. pixelsPerUnit is the smallest scale in pixels of that plane. Positions too close to an
// edge are skipped.
inline Check ExpectProjectedSphere(const std::function<GfVec2f(const GfVec3d&)>& project, double pixelsPerUnit,
    const GfRect2i& data, int bufferWidth, int bufferHeight)
{
    // The radius of the sphere silhouette in the plane of its center
    const double radius = kFramingSphereRadius;
    const double silhouette = radius * 6.0 / std::sqrt(36.0 - radius * radius);
    const double margin = 4.0;

    std::vector<Check> checks;
    const double offsets[] = {-2.4, -1.7, -0.4, 0.0, 0.4, 1.7, 2.4};
    for (double x : offsets) {
        for (double y : offsets) {
            const GfVec2f position = project(GfVec3d(x, y, 0));
            const double px = position[0] * bufferWidth;
            const double py = position[1] * bufferHeight;
            if (px < margin || py < margin || px > bufferWidth - margin || py > bufferHeight - margin)
                continue;
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

// The sphere seen by the 36x27 camera made by DefineCamera, projected with the framing like ApplyToProjectionMatrix
// does (the camera has the aspect of the display windows used, so the window policy doesn't matter). Camera changes
// that scale or move the film, e.g. the screen window or the focal length, can be checked with the display window
// they are equivalent to.
inline Check ExpectSphere(const CameraUtilFraming& framing, int bufferWidth = kFramingWidth,
    int bufferHeight = kFramingHeight)
{
    const double pixelsPerUnit = framing.displayWindow.GetSize()[0] / (2.0 * 6.0 * 18.0 / 35.0);
    return ExpectProjectedSphere(
        [=](const GfVec3d& point) { return FramedScreenPosition(point, framing, bufferWidth, bufferHeight); },
        pixelsPerUnit, framing.dataWindow, bufferWidth, bufferHeight);
}

// Sets the aperture of the camera made by DefineCamera, to give it another aspect ratio
inline void SetCameraAperture(const UsdStageRefPtr& stage, const SdfPath& camera, float horizontal, float vertical)
{
    UsdGeomCamera usdCamera(stage->GetPrimAtPath(camera));
    usdCamera.CreateHorizontalApertureAttr().Set(horizontal);
    usdCamera.CreateVerticalApertureAttr().Set(vertical);
}

// The sphere seen by a camera made by DefineCamera with this aperture, conformed to the display window with the
// window policy and framed by CameraUtilFraming::ApplyToProjectionMatrix: what Storm draws.
inline Check ExpectConformedSphere(const CameraUtilFraming& framing, CameraUtilConformWindowPolicy windowPolicy,
    float horizontalAperture, float verticalAperture, int bufferWidth = kFramingWidth,
    int bufferHeight = kFramingHeight)
{
    GfCamera camera;
    camera.SetFocalLength(35.f);
    camera.SetHorizontalAperture(horizontalAperture);
    camera.SetVerticalAperture(verticalAperture);
    const GfMatrix4d projection =
        framing.ApplyToProjectionMatrix(camera.GetFrustum().ComputeProjectionMatrix(), windowPolicy);
    const GfRect2i data = framing.dataWindow;
    // The camera is at (0, 0, 6) looking down -Z, NDC maps to the data window, y down
    const auto project = [=](const GfVec3d& point) -> GfVec2f {
        const GfVec4d clip = GfVec4d(point[0], point[1], point[2] - 6.0, 1.0) * projection;
        const double ndcX = clip[0] / clip[3];
        const double ndcY = clip[1] / clip[3];
        return GfVec2f(static_cast<float>((data.GetMinX() + data.GetWidth() * (0.5 + 0.5 * ndcX)) / bufferWidth),
            static_cast<float>((data.GetMinY() + data.GetHeight() * (0.5 - 0.5 * ndcY)) / bufferHeight));
    };
    const GfVec2f origin = project(GfVec3d(0, 0, 0));
    const GfVec2f unitX = project(GfVec3d(1, 0, 0));
    const GfVec2f unitY = project(GfVec3d(0, 1, 0));
    const double pixelsPerUnit = std::min(std::abs(unitX[0] - origin[0]) * bufferWidth,
        std::abs(unitY[1] - origin[1]) * bufferHeight);
    return ExpectProjectedSphere(project, pixelsPerUnit, data, bufferWidth, bufferHeight);
}

// Renders the sphere through a camera with this aperture, whose aspect ratio differs from the 160x120 buffer, with
// each window policy, then with a framing (overscan, film translate), with a render settings resolution and without
// it. The window policy is the camera's (CameraUtilFit) until the first override.
inline int RunWindowPolicyScenario(float horizontalAperture, float verticalAperture, int referenceStep = -1)
{
    const GfRect2i full(GfVec2i(0, 0), kFramingWidth, kFramingHeight);
    const CameraUtilFraming none = FullFraming();
    const CameraUtilFraming overscan = MakeFraming(GfRange2f(GfVec2f(40, 30), GfVec2f(120, 90)), full);
    const CameraUtilFraming filmTranslate = MakeFraming(GfRange2f(GfVec2f(20, 10), GfVec2f(180, 130)), full);
    const auto expect = [=](const CameraUtilFraming& framing, CameraUtilConformWindowPolicy policy) {
        return ExpectConformedSphere(framing, policy, horizontalAperture, verticalAperture);
    };
    const auto policyStep = [](const std::string& description, const Check& check,
                                std::optional<CameraUtilConformWindowPolicy> policy,
                                std::optional<CameraUtilFraming> framing = std::nullopt,
                                std::vector<std::pair<std::string, VtValue>> renderSettings = {}) {
        Step step;
        step.description = description;
        step.check = check;
        step.windowPolicy = policy;
        step.framing = framing;
        step.renderSettings = std::move(renderSettings);
        return step;
    };

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.referenceStep = referenceStep;
    const SdfPath camera = scenario.camera;
    scenario.createStage = [=]() {
        UsdStageRefPtr stage = CreateFramingStage(camera);
        SetCameraAperture(stage, camera, horizontalAperture, verticalAperture);
        return stage;
    };
    scenario.steps = {
        policyStep("camera window policy, fit", expect(none, CameraUtilFit), std::nullopt),
        policyStep("crop", expect(none, CameraUtilCrop), CameraUtilCrop),
        policyStep("match vertically", expect(none, CameraUtilMatchVertically), CameraUtilMatchVertically),
        policyStep("match horizontally", expect(none, CameraUtilMatchHorizontally), CameraUtilMatchHorizontally),
        policyStep("don't conform", expect(none, CameraUtilDontConform), CameraUtilDontConform),
        policyStep("fit with an overscan framing", expect(overscan, CameraUtilFit), CameraUtilFit, overscan),
        policyStep("crop with a film translate framing", expect(filmTranslate, CameraUtilCrop), CameraUtilCrop,
            filmTranslate),
        // A render settings resolution ignores the host framing and its window policy, as before (husk relies on
        // it): the camera matches its horizontal aperture across the image
        policyStep("render settings resolution, the window policy is ignored",
            expect(none, CameraUtilMatchHorizontally), std::nullopt, std::nullopt,
            {{"resolution", VtValue(GfVec2i(kFramingWidth, kFramingHeight))}}),
        policyStep("render settings resolution cleared, no framing, fit", expect(none, CameraUtilFit), CameraUtilFit,
            none, {{"resolution", VtValue(GfVec2i(0, 0))}}),
    };
    return Run(scenario);
}

} // namespace interactive_usd_test
