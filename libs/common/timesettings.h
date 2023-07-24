//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

struct TimeSettings {
    TimeSettings() : motionBlur(false), frame(1.f),  motionStart(1.f), motionEnd(1.f) {}

    bool motionBlur;
    float frame;
    float motionStart;
    float motionEnd;

    float start() const { return motionBlur ? motionStart + frame : frame; }
    float end() const { return motionBlur ? motionEnd + frame : frame; }
};