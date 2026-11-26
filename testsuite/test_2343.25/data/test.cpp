// Render regions set by the resolution and dataWindowNDC render settings, as Houdini sends them for a cropped render:
// the host buffer is the size of the region, the resolution is the full image's. Covers the fix of the regions
// cropped on a single axis (#2564): the resolution only replaced the host framing when both of its axes were
// different from the buffer's, so a region cropped in x or y only was rendered from the wrong image size. See
// interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kRed("/World/red");
const SdfPath kGreen("/World/green");
// The red sphere is in the top left quarter of the image, the green one in the bottom right quarter
const GfVec3d kRedPosition(-1.2, 0.8, 0);
const GfVec3d kGreenPosition(1.2, -0.8, 0);

void DefineSphere(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3d& position, const GfVec3f& color)
{
    UsdGeomSphere::Define(stage, path).CreateRadiusAttr().Set(0.6);
    SetTranslate(stage, path, position);
    BindMaterial(stage, path, DefinePreviewMaterial(stage, path.AppendChild(TfToken("material")), color));
}

CameraUtilFraming BufferFraming(int width, int height)
{
    return CameraUtilFraming(GfRect2i(GfVec2i(0, 0), width, height));
}

// The settings of a region of the 160x120 image, given in y-up NDC
std::vector<std::pair<std::string, VtValue>> RegionSettings(const GfVec4f& windowNDC)
{
    return {{"resolution", VtValue(GfVec2i(160, 120))}, {"dataWindowNDC", VtValue(windowNDC)}};
}

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    // Positions in the full 160x120 image
    const GfVec2f red = ScreenPosition(kRedPosition);
    // Also checked in the cropped buffers, where the green sphere would be if the full image was squeezed in them
    const GfVec2f green = ScreenPosition(kGreenPosition);
    // Position in the left half of the image (x in [0, 0.5]) of a position of the full image
    auto inLeftHalf = [](const GfVec2f& p) { return GfVec2f(p[0] * 2.f, p[1]); };
    // Position in the top half of the image (y in [0, 0.5]) of a position of the full image
    auto inTopHalf = [](const GfVec2f& p) { return GfVec2f(p[0], p[1] * 2.f); };
    // Above the red sphere, and on its left, in the background
    const GfVec2f aboveRed = ScreenPosition(kRedPosition + GfVec3d(0, 0.9, 0));
    const GfVec2f leftOfRed = ScreenPosition(kRedPosition - GfVec3d(0.9, 0, 0));

    const Check fullImage = AllOf({ExpectColor(Color::Red, red), ExpectColor(Color::Green, green),
        ExpectColor(Color::Empty, aboveRed), ExpectColor(Color::Empty, leftOfRed)});

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));
        DefineDistantLight(stage, SdfPath("/World/light"));
        DefineSphere(stage, kRed, kRedPosition, GfVec3f(0.8f, 0.05f, 0.05f));
        DefineSphere(stage, kGreen, kGreenPosition, GfVec3f(0.05f, 0.8f, 0.05f));
        return stage;
    };

    Step full{"initial render, full image", nullptr, fullImage};
    Step leftHalf{"region of the left half, cropped in x only, in an 80x120 buffer", nullptr,
        AllOf({ExpectColor(Color::Red, inLeftHalf(red)), ExpectColor(Color::Empty, inLeftHalf(aboveRed)),
            ExpectColor(Color::Empty, inLeftHalf(leftOfRed)), ExpectColor(Color::Empty, green)})};
    leftHalf.bufferSize = GfVec2i(80, 120);
    leftHalf.framing = BufferFraming(80, 120);
    leftHalf.renderSettings = RegionSettings(GfVec4f(0.f, 0.f, 0.5f, 1.f));
    Step topHalf{"region of the top half, cropped in y only, in a 160x60 buffer", nullptr,
        AllOf({ExpectColor(Color::Red, inTopHalf(red)), ExpectColor(Color::Empty, inTopHalf(aboveRed)),
            ExpectColor(Color::Empty, inTopHalf(leftOfRed)),
            ExpectColor(Color::Empty, green)})};
    topHalf.bufferSize = GfVec2i(160, 60);
    topHalf.framing = BufferFraming(160, 60);
    topHalf.renderSettings = RegionSettings(GfVec4f(0.f, 0.5f, 1.f, 1.f));
    Step bottomRight{"region of the bottom right quarter, cropped in both axes, in an 80x60 buffer", nullptr,
        AllOf({ExpectColor(Color::Green, GfVec2f(green[0] * 2.f - 1.f, green[1] * 2.f - 1.f)),
            ExpectColor(Color::Empty, GfVec2f(0.1f, 0.1f))})};
    bottomRight.bufferSize = GfVec2i(80, 60);
    bottomRight.framing = BufferFraming(80, 60);
    bottomRight.renderSettings = RegionSettings(GfVec4f(0.5f, 0.f, 1.f, 0.5f));
    Step cleared{"render settings cleared, full image in a 160x120 buffer", nullptr, fullImage};
    cleared.bufferSize = GfVec2i(160, 120);
    cleared.framing = BufferFraming(160, 120);
    cleared.renderSettings = {{"resolution", VtValue(GfVec2i(0, 0))},
        {"dataWindowNDC", VtValue(GfVec4f(0.f, 0.f, 1.f, 1.f))}};

    scenario.steps = {full, leftHalf, topHalf, bottomRight, cleared};
    return Run(scenario);
}
