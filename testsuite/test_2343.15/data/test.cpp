// Interactive edits of the camera parameters: focal length, apertures, clipping range, projection and depth of field.
// Only run with the render delegate, as the procedural renders the stage with an arnold camera made by the framework.
// See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

#include <cstdio>

using namespace interactive_usd_test;

namespace {

// Partially covered pixels, as on the blurred silhouette of an out of focus object
Check ExpectPartialCoverage(float u, float v)
{
    return [u, v](const Image& image) -> std::string {
        const float alpha = image.Average(u, v)[3];
        if (alpha > 0.02f && alpha < 0.98f)
            return std::string();
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "expected a partial coverage at (%.2f, %.2f), got an alpha of %.4f", u, v,
            alpha);
        return buffer;
    };
}

} // namespace

int main(int, char**)
{
    // Outside the sphere with the initial camera, inside once zoomed in
    const GfVec2f probe = ScreenPosition(GfVec3d(1.3, 0, 0));
    // Inside the sphere with a perspective camera of focal length 70 and horizontal aperture 30, outside with an
    // orthographic camera of horizontal aperture 30
    const GfVec2f orthoProbe(0.86f, 0.5f);
    const GfVec2f blurProbe(0.93f, 0.5f);

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        const SdfPath sphere("/World/sphere");
        UsdGeomSphere::Define(stage, sphere).CreateRadiusAttr().Set(1.0);
        BindMaterial(stage, sphere, DefinePreviewMaterial(stage, SdfPath("/World/materials/grey"), GfVec3f(0.5f)));
        return stage;
    };

    auto camera = [&](const UsdStageRefPtr& stage) { return UsdGeomCamera(stage->GetPrimAtPath(scenario.camera)); };
    auto setApertures = [=](const UsdStageRefPtr& stage, float horizontal) {
        camera(stage).GetHorizontalApertureAttr().Set(horizontal);
        camera(stage).GetVerticalApertureAttr().Set(horizontal * 0.75f);
    };
    scenario.steps = {
        {"initial render", nullptr, AllOf({ExpectColor(Color::Grey), ExpectColor(Color::Empty, probe)})},
        {"focal length change from 35 to 70",
            [=](const UsdStageRefPtr& stage) { camera(stage).GetFocalLengthAttr().Set(70.f); },
            AllOf({ExpectColor(Color::Grey), ExpectColor(Color::Grey, probe)})},
        {"apertures change, twice bigger", [=](const UsdStageRefPtr& stage) { setApertures(stage, 72.f); },
            AllOf({ExpectColor(Color::Grey), ExpectColor(Color::Empty, probe)})},
        {"clipping range change, the near plane being behind the sphere",
            [=](const UsdStageRefPtr& stage) { camera(stage).CreateClippingRangeAttr().Set(GfVec2f(7.f, 1000.f)); },
            ExpectColor(Color::Empty)},
        {"clipping range change back",
            [=](const UsdStageRefPtr& stage) { camera(stage).GetClippingRangeAttr().Set(GfVec2f(0.1f, 1000.f)); },
            ExpectColor(Color::Grey)},
        {"projection change to orthographic, 7.2 units wide",
            [=](const UsdStageRefPtr& stage) {
                camera(stage).CreateProjectionAttr().Set(UsdGeomTokens->orthographic);
            },
            AllOf({ExpectColor(Color::Grey), ExpectColor(Color::Empty, probe)})},
        {"orthographic apertures change, 3 units wide", [=](const UsdStageRefPtr& stage) { setApertures(stage, 30.f); },
            AllOf({ExpectColor(Color::Grey), ExpectColor(Color::Grey, probe), ExpectColor(Color::Empty, orthoProbe)})},
        {"projection change back to perspective",
            [=](const UsdStageRefPtr& stage) { camera(stage).GetProjectionAttr().Set(UsdGeomTokens->perspective); },
            AllOf({ExpectColor(Color::Grey), ExpectColor(Color::Grey, orthoProbe), ExpectColor(Color::Empty, blurProbe)})},
        {"depth of field, focusing 1 unit in front of the sphere",
            [=](const UsdStageRefPtr& stage) {
                camera(stage).CreateFStopAttr().Set(4.f);
                camera(stage).CreateFocusDistanceAttr().Set(5.f);
            },
            ExpectPartialCoverage(blurProbe[0], blurProbe[1])},
        {"depth of field disabled",
            [=](const UsdStageRefPtr& stage) { camera(stage).GetFStopAttr().Set(0.f); },
            AllOf({ExpectColor(Color::Grey), ExpectColor(Color::Empty, blurProbe)})},
    };
    return Run(scenario);
}
