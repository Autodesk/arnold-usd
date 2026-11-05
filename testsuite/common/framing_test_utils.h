// Helpers shared by the interactive tests of the host CameraUtilFraming (test_2819*), see interactive_usd_test.h
#pragma once

#include "interactive_usd_test.h"

#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

#include <cmath>
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

// Checks the pixels at a grid of world positions in the plane of the sphere center, projected with the framing like
// ApplyToProjectionMatrix does: the sphere where it is expected in the data window, the background around it, and
// nothing outside of the data window. Positions too close to an edge are skipped. Camera changes that scale or move
// the film, e.g. the screen window or the focal length, can be checked with the display window they are equivalent to.
inline Check ExpectSphere(const CameraUtilFraming& framing, int bufferWidth = kFramingWidth,
    int bufferHeight = kFramingHeight)
{
    // The scale in pixels of the plane of the sphere center, and the radius of the sphere silhouette
    const double pixelsPerUnit = framing.displayWindow.GetSize()[0] / (2.0 * 6.0 * 18.0 / 35.0);
    const double radius = kFramingSphereRadius;
    const double silhouette = radius * 6.0 / std::sqrt(36.0 - radius * radius);
    const double margin = 4.0;

    std::vector<Check> checks;
    const double offsets[] = {-2.4, -1.7, -0.4, 0.0, 0.4, 1.7, 2.4};
    for (double x : offsets) {
        for (double y : offsets) {
            const GfVec2f position = FramedScreenPosition(GfVec3d(x, y, 0), framing, bufferWidth, bufferHeight);
            const double px = position[0] * bufferWidth;
            const double py = position[1] * bufferHeight;
            if (px < margin || py < margin || px > bufferWidth - margin || py > bufferHeight - margin)
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

} // namespace interactive_usd_test
