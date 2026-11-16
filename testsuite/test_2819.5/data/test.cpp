// The render settings resolution takes precedence over the host framing. See framing_test_utils.h.
#include <framing_test_utils.h>

using namespace interactive_usd_test;

INTERACTIVE_USD_TEST_MAIN()
{
    const GfRect2i full(GfVec2i(0, 0), kFramingWidth, kFramingHeight);
    const CameraUtilFraming none = FullFraming();
    const CameraUtilFraming zoom = MakeFraming(GfRange2f(GfVec2f(-80, -60), GfVec2f(240, 180)), full);
    const CameraUtilFraming filmTranslate = MakeFraming(GfRange2f(GfVec2f(20, 10), GfVec2f(180, 130)), full);
    const VtValue resolution(GfVec2i(kFramingWidth, kFramingHeight));
    const VtValue noResolution(GfVec2i(0, 0));

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.framing = zoom;
    scenario.createStage = [&]() { return CreateFramingStage(scenario.camera); };

    scenario.steps = {
        {"zoom framing", nullptr, ExpectSphere(zoom)},
        {"render settings resolution, the framing is ignored", nullptr, ExpectSphere(none), {}, std::nullopt,
            std::nullopt, {{"resolution", resolution}}},
        {"film translate framing with a render settings resolution, the framing is ignored", nullptr,
            ExpectSphere(none), {}, filmTranslate},
        {"render settings resolution cleared, the framing applies", nullptr, ExpectSphere(filmTranslate), {},
            std::nullopt, std::nullopt, {{"resolution", noResolution}}},
        {"no framing", nullptr, ExpectSphere(none), {}, none},
    };
    return Run(scenario);
}
