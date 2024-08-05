//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <pxr/base/gf/interval.h>

struct TimeSettings {
    TimeSettings() : motionBlur(false), frame(1.f),  motionStart(1.f), motionEnd(1.f) {}

    bool motionBlur;
    float frame;
    float motionStart;
    float motionEnd;

    float start() const { return motionBlur ? motionStart + frame : frame; }
    float end() const { return motionBlur ? motionEnd + frame : frame; }
};


PXR_NAMESPACE_OPEN_SCOPE

// Utility function to compute the number of keys required
template <typename TimeSampledType>
int ComputeNumKeys(const TimeSampledType &attr, const TimeSettings &time) {
    GfInterval interval(time.start(), time.end(), false, false);
    std::vector<double> timeSamples;
    attr.GetTimeSamplesInInterval(interval, &timeSamples);
    // We add the start and end keys (interval has open bounds)
    int boundaryKeys = 2;
    // TimeSamples is sorted, we remove the boundaries if they are already in the timeSamples
    if (!timeSamples.empty() && timeSamples[0] == interval.GetMin()) boundaryKeys--;
    if (!timeSamples.empty() && timeSamples.back() == interval.GetMax()) boundaryKeys--;
    return timeSamples.size() + boundaryKeys;
}
PXR_NAMESPACE_CLOSE_SCOPE