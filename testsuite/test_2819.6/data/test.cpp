// A 16:9 camera in the 4:3 buffer: wider than the display window, fit matches its horizontal aperture and crop its
// vertical aperture. See framing_test_utils.h.
#include <framing_test_utils.h>

using namespace interactive_usd_test;

int main(int, char**)
{
    // 36x20.25 mm, the reference image is the crop, the window policy that differs from Arnold's own horizontal fit
    return RunWindowPolicyScenario(36.f, 20.25f, 1);
}
