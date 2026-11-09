// A 2:3 (portrait) camera in the 4:3 buffer: taller than the display window, fit matches its vertical aperture and
// crop its horizontal aperture. See framing_test_utils.h.
#include <framing_test_utils.h>

using namespace interactive_usd_test;

int main(int, char**)
{
    // 24x36 mm, the reference image is the last render, fit without a framing
    return RunWindowPolicyScenario(24.f, 36.f);
}
