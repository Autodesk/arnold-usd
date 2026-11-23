// Reallocation of the host's render buffer between renders, with a host framing. See framing_test_utils.h.
#include <framing_test_utils.h>

using namespace interactive_usd_test;

INTERACTIVE_USD_TEST_MAIN()
{
    const GfRect2i full(GfVec2i(0, 0), kFramingWidth, kFramingHeight);
    const CameraUtilFraming none = FullFraming();
    const CameraUtilFraming filmTranslate = MakeFraming(GfRange2f(GfVec2f(20, 10), GfVec2f(180, 130)), full);
    // The same film translate in a buffer twice as big
    const GfRect2i full2x(GfVec2i(0, 0), 2 * kFramingWidth, 2 * kFramingHeight);
    const CameraUtilFraming filmTranslate2x = MakeFraming(GfRange2f(GfVec2f(40, 20), GfVec2f(360, 260)), full2x);
    // An overscan in a 240x180 buffer: the film is half the size of the buffer
    const GfRect2i full240(GfVec2i(0, 0), 240, 180);
    const CameraUtilFraming overscan240 = MakeFraming(GfRange2f(GfVec2f(60, 45), GfVec2f(180, 135)), full240);

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.framing = filmTranslate;
    scenario.createStage = [&]() { return CreateFramingStage(scenario.camera); };

    scenario.steps = {
        {"film translate framing", nullptr, ExpectSphere(filmTranslate)},
        {"buffer twice as big", nullptr, ExpectSphere(filmTranslate2x, 320, 240), {}, filmTranslate2x, GfVec2i(320, 240)},
        {"240x180 buffer with an overscan", nullptr, ExpectSphere(overscan240, 240, 180), {}, overscan240,
            GfVec2i(240, 180)},
        {"back to the initial buffer, no framing", nullptr, ExpectSphere(none), {}, none, GfVec2i(160, 120)},
    };
    return Run(scenario);
}
