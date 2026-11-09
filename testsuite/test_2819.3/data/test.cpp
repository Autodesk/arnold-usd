// Sequence of host framings: the film position and the filled region must follow each of them. See
// framing_test_utils.h.
#include <framing_test_utils.h>

using namespace interactive_usd_test;

int main(int, char**)
{
    const GfRect2i full(GfVec2i(0, 0), kFramingWidth, kFramingHeight);
    const GfRange2f fullRange(GfVec2f(0, 0), GfVec2f(kFramingWidth, kFramingHeight));
    const CameraUtilFraming none = FullFraming();
    const CameraUtilFraming pan = MakeFraming(GfRange2f(GfVec2f(30, 20), GfVec2f(190, 140)), full);
    // A display window with a fractional origin, as maya-hydra solves it
    const CameraUtilFraming fractionalPan = MakeFraming(GfRange2f(GfVec2f(30.6f, 20.4f), GfVec2f(190.6f, 140.4f)), full);
    // A small render region around the center of the sphere
    const CameraUtilFraming smallRegion = MakeFraming(fullRange, GfRect2i(GfVec2i(60, 45), 40, 30));
    // A render region having the center of the sphere at its top left corner
    const CameraUtilFraming cornerRegion = MakeFraming(fullRange, GfRect2i(GfVec2i(0, 0), 80, 60));
    const CameraUtilFraming crop =
        MakeFraming(GfRange2f(GfVec2f(60, 30), GfVec2f(140, 90)), GfRect2i(GfVec2i(60, 30), 80, 60));
    // An overscan and a render region
    const CameraUtilFraming overscanRegion =
        MakeFraming(GfRange2f(GfVec2f(40, 30), GfVec2f(120, 90)), GfRect2i(GfVec2i(60, 45), 40, 30));

    Scenario scenario;
    scenario.methods = RenderDelegate;
    scenario.createStage = [&]() { return CreateFramingStage(scenario.camera); };

    scenario.steps = {
        {"no framing", nullptr, ExpectSphere(none)},
        {"pan", nullptr, ExpectSphere(pan), {}, pan},
        {"pan with a fractional origin", nullptr, ExpectSphere(fractionalPan), {}, fractionalPan},
        {"small render region", nullptr, ExpectSphere(smallRegion), {}, smallRegion},
        {"render region moved", nullptr, ExpectSphere(cornerRegion), {}, cornerRegion},
        {"display window == data window, away from the origin", nullptr, ExpectSphere(crop), {}, crop},
        {"overscan and render region", nullptr, ExpectSphere(overscanRegion), {}, overscanRegion},
        {"back to no framing", nullptr, ExpectSphere(none), {}, none},
    };
    return Run(scenario);
}
