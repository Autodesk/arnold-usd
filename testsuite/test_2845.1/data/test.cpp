// Interactive edits of the color space of a Houdini gaussian splat (points with a GS_Alpha primvar): the inherited
// UsdColorSpaceAPI colorSpace:name of its parent, the colorSpace metadata of its displayColor, and
// primvars:arnold:gs_input_color_space, which overrides them. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/pxr.h>

// UsdColorSpaceAPI was added in USD 25.05
#if PXR_VERSION >= 2505
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/colorSpaceAPI.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kGroup("/World/houdini");
const SdfPath kSplat("/World/houdini/points");

// A splat of mid grey (0.5) is about 0.21 when decoded as sRGB, Arnold's default, and 0.5 in a linear space. The
// splats aren't fully opaque at the center, hence the margins.
constexpr float kSrgbMin = 0.12f;
constexpr float kSrgbMax = 0.3f;
constexpr float kLinearMin = 0.35f;
constexpr float kLinearMax = 0.6f;

Check ExpectGrey(float minValue, float maxValue, const GfVec2f& position)
{
    return [=](const Image& image) -> std::string {
        const GfVec4f c = image.Average(position[0], position[1]);
        const float value = (c[0] + c[1] + c[2]) / 3.f;
        if (value >= minValue && value <= maxValue && std::abs(c[0] - c[1]) < 0.02f && std::abs(c[1] - c[2]) < 0.02f)
            return {};
        return TfStringPrintf("expected a grey in [%.2f, %.2f] for the splat at (%.2f, %.2f), got (%.4f, %.4f, %.4f)",
            minValue, maxValue, position[0], position[1], c[0], c[1], c[2]);
    };
}

UsdAttribute DisplayColor(const UsdStageRefPtr& stage)
{
    return UsdGeomPoints(stage->GetPrimAtPath(kSplat)).GetDisplayColorAttr();
}

UsdAttribute ArnoldColorSpace(const UsdStageRefPtr& stage)
{
    return stage->GetPrimAtPath(kSplat).CreateAttribute(
        TfToken("primvars:arnold:gs_input_color_space"), SdfValueTypeNames->String, false, SdfVariabilityUniform);
}

// Modes in which the render delegate can't get the USD color space: UsdImaging gives it to Hydra 2 only
constexpr unsigned kHydra1 = ProceduralHydra1 | DelegateHydra1;
const char* kHydra1Issue = "the USD color space of the splats is only supported with Hydra 2";
// UsdImaging only invalidates the prim whose colorSpace:name changes, not its descendants inheriting it
const char* kInheritedIssue = "Hydra doesn't update the color space inherited from a parent";
// Preexisting, unrelated to the color space: the splat renders black whatever its color space
const char* kLegacyIssue = "the legacy reader doesn't render Houdini splats in interactive sessions";

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const GfVec2f center = ScreenPosition(GfVec3d(0, 0, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));

        // A grey Houdini splat, whose displayColor is linear through the colorSpace:name of its parent
        UsdColorSpaceAPI::Apply(UsdGeomXform::Define(stage, kGroup).GetPrim())
            .CreateColorSpaceNameAttr()
            .Set(TfToken("lin_rec709_scene"));
        auto points = UsdGeomPoints::Define(stage, kSplat);
        points.CreatePointsAttr().Set(VtVec3fArray{GfVec3f(0.f)});
        points.CreateDisplayColorPrimvar(UsdGeomTokens->vertex).Set(VtVec3fArray{GfVec3f(0.5f)});
        UsdGeomPrimvarsAPI primvars(points.GetPrim());
        primvars.CreatePrimvar(TfToken("GS_Alpha"), SdfValueTypeNames->FloatArray, UsdGeomTokens->vertex)
            .Set(VtFloatArray{1.f});
        primvars.CreatePrimvar(TfToken("scale"), SdfValueTypeNames->Float3Array, UsdGeomTokens->vertex)
            .Set(VtVec3fArray{GfVec3f(0.5f)});
        primvars.CreatePrimvar(TfToken("orient"), SdfValueTypeNames->QuatfArray, UsdGeomTokens->vertex)
            .Set(VtQuatfArray{GfQuatf(1.f)});
        return stage;
    };

    const std::vector<KnownIssue> kHydra1AndLegacyIssues = {
        {kHydra1, kHydra1Issue}, {ProceduralLegacyReader, kLegacyIssue}};
    scenario.steps = {
        {"initial render, the splat inherits a linear color space", nullptr,
            ExpectGrey(kLinearMin, kLinearMax, center), kHydra1AndLegacyIssues},
        {"the parent's colorSpace:name becomes sRGB",
            [](const UsdStageRefPtr& stage) {
                UsdColorSpaceAPI(stage->GetPrimAtPath(kGroup))
                    .GetColorSpaceNameAttr()
                    .Set(TfToken("srgb_rec709_scene"));
            },
            ExpectGrey(kSrgbMin, kSrgbMax, center),
            {{ProceduralHydra2 | DelegateHydra2, kInheritedIssue}, {ProceduralLegacyReader, kLegacyIssue}}},
        {"displayColor gets a raw colorSpace metadata, leaving the colors untouched",
            [](const UsdStageRefPtr& stage) { DisplayColor(stage).SetColorSpace(TfToken("raw")); },
            ExpectGrey(kLinearMin, kLinearMax, center), kHydra1AndLegacyIssues},
        {"primvars:arnold:gs_input_color_space overrides it with sRGB",
            [](const UsdStageRefPtr& stage) { ArnoldColorSpace(stage).Set(std::string("sRGB")); },
            ExpectGrey(kSrgbMin, kSrgbMax, center), {{ProceduralLegacyReader, kLegacyIssue}}},
        {"primvars:arnold:gs_input_color_space is removed, back to the raw colorSpace metadata",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kSplat).RemoveProperty(TfToken("primvars:arnold:gs_input_color_space"));
            },
            ExpectGrey(kLinearMin, kLinearMax, center), kHydra1AndLegacyIssues},
    };
    // The sRGB override renders the same with Hydra 1, compare it against the reference image
    scenario.referenceStep = 3;
    return Run(scenario);
}

#else

INTERACTIVE_USD_TEST_MAIN()
{
    return 0;
}

#endif
