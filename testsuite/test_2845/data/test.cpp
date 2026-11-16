// Interactive edits of the color space of a ParticleField3DGaussianSplat: the UsdColorSpaceAPI colorSpace:name of the
// prim, the colorSpace metadata of the spherical harmonics coefficients, and primvars:arnold:gs_input_color_space,
// which overrides them. See interactive_usd_test.h.
#include <interactive_usd_test.h>

#include <pxr/pxr.h>

// The gaussian splat schema was added in USD 26.03
#if PXR_VERSION >= 2603
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/colorSpaceAPI.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdVol/particleField3DGaussianSplat.h>

using namespace interactive_usd_test;

namespace {

const SdfPath kSplat("/World/splat");

// A splat of mid grey (0.5) is about 0.21 when decoded as sRGB, Arnold's default, and 0.5 in a linear space. The
// splats aren't fully opaque at the center, hence the margins.
constexpr float kSrgbMin = 0.12f;
constexpr float kSrgbMax = 0.3f;
constexpr float kLinearMin = 0.35f;
constexpr float kLinearMax = 0.6f;

Check ExpectGrey(float minValue, float maxValue, const GfVec2f& position, const char* what)
{
    return [=](const Image& image) -> std::string {
        const GfVec4f c = image.Average(position[0], position[1]);
        const float value = (c[0] + c[1] + c[2]) / 3.f;
        if (value >= minValue && value <= maxValue && std::abs(c[0] - c[1]) < 0.02f && std::abs(c[1] - c[2]) < 0.02f)
            return {};
        return TfStringPrintf("expected a grey in [%.2f, %.2f] for the %s at (%.2f, %.2f), got (%.4f, %.4f, %.4f)",
            minValue, maxValue, what, position[0], position[1], c[0], c[1], c[2]);
    };
}

UsdAttribute ShCoefficients(const UsdStageRefPtr& stage)
{
    return UsdVolParticleField3DGaussianSplat(stage->GetPrimAtPath(kSplat))
        .GetRadianceSphericalHarmonicsCoefficientsAttr();
}

UsdAttribute ArnoldColorSpace(const UsdStageRefPtr& stage)
{
    return stage->GetPrimAtPath(kSplat).CreateAttribute(
        TfToken("primvars:arnold:gs_input_color_space"), SdfValueTypeNames->String, false, SdfVariabilityUniform);
}

// Modes in which the render delegate can't get the USD color space: UsdImaging gives it to Hydra 2 only
constexpr unsigned kHydra1 = ProceduralHydra1 | DelegateHydra1;
const char* kHydra1Issue = "the USD color space of the splats is only supported with Hydra 2";

} // namespace

INTERACTIVE_USD_TEST_MAIN()
{
    const GfVec2f left = ScreenPosition(GfVec3d(-1.2, 0, 0));

    Scenario scenario;
    scenario.createStage = [&]() {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform::Define(stage, SdfPath("/World"));
        DefineCamera(stage, scenario.camera, GfVec3d(0, 0, 6));

        // A grey ParticleField3DGaussianSplat on the left, without any color space. A zero DC coefficient decodes to
        // a grey of 0.5.
        auto splat = UsdVolParticleField3DGaussianSplat::Define(stage, kSplat);
        splat.CreatePositionsAttr().Set(VtVec3fArray{GfVec3f(-1.2f, 0.f, 0.f)});
        splat.CreateScalesAttr().Set(VtVec3fArray{GfVec3f(0.5f)});
        splat.CreateOrientationsAttr().Set(VtQuatfArray{GfQuatf(1.f)});
        splat.CreateOpacitiesAttr().Set(VtFloatArray{1.f});
        splat.CreateRadianceSphericalHarmonicsDegreeAttr().Set(0);
        splat.CreateRadianceSphericalHarmonicsCoefficientsAttr().Set(VtVec3fArray{GfVec3f(0.f)});
        return stage;
    };

    scenario.steps = {
        {"initial render, the splat is sRGB by default", nullptr, ExpectGrey(kSrgbMin, kSrgbMax, left, "splat")},
        {"the splat gets a linear colorSpace:name",
            [](const UsdStageRefPtr& stage) {
                UsdColorSpaceAPI::Apply(stage->GetPrimAtPath(kSplat))
                    .CreateColorSpaceNameAttr()
                    .Set(TfToken("lin_rec709_scene"));
            },
            ExpectGrey(kLinearMin, kLinearMax, left, "splat"), {{kHydra1, kHydra1Issue}}},
        {"primvars:arnold:gs_input_color_space overrides it with sRGB",
            [](const UsdStageRefPtr& stage) { ArnoldColorSpace(stage).Set(std::string("sRGB")); },
            ExpectGrey(kSrgbMin, kSrgbMax, left, "splat")},
        {"primvars:arnold:gs_input_color_space is removed, back to the linear colorSpace:name",
            [](const UsdStageRefPtr& stage) {
                stage->GetPrimAtPath(kSplat).RemoveProperty(TfToken("primvars:arnold:gs_input_color_space"));
            },
            ExpectGrey(kLinearMin, kLinearMax, left, "splat"), {{kHydra1, kHydra1Issue}}},
        {"the coefficients get an sRGB colorSpace metadata, stronger than the prim's colorSpace:name",
            [](const UsdStageRefPtr& stage) { ShCoefficients(stage).SetColorSpace(TfToken("srgb_rec709_scene")); },
            ExpectGrey(kSrgbMin, kSrgbMax, left, "splat")},
        {"the colorSpace metadata becomes raw, leaving the colors untouched",
            [](const UsdStageRefPtr& stage) { ShCoefficients(stage).SetColorSpace(TfToken("raw")); },
            ExpectGrey(kLinearMin, kLinearMax, left, "splat"), {{kHydra1, kHydra1Issue}}},
        {"the colorSpace metadata is cleared and the colorSpace:name becomes sRGB",
            [](const UsdStageRefPtr& stage) {
                ShCoefficients(stage).ClearColorSpace();
                UsdColorSpaceAPI(stage->GetPrimAtPath(kSplat))
                    .GetColorSpaceNameAttr()
                    .Set(TfToken("srgb_rec709_scene"));
            },
            ExpectGrey(kSrgbMin, kSrgbMax, left, "splat")},
    };
    return Run(scenario);
}

#else

INTERACTIVE_USD_TEST_MAIN()
{
    return 0;
}

#endif
