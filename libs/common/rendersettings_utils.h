//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once
#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>
#include <ai.h>
#include "timesettings.h"
#include "api_adapter.h"

// TODO: get rid of that and mode the code in the render_option.h

PXR_NAMESPACE_OPEN_SCOPE

struct ArnoldAOVTypes {
    const char *outputString;
    const AtString aovWrite;
    const AtString userData;
    bool isHalf;

    ArnoldAOVTypes(const char *_outputString, const AtString &_aovWrite, const AtString &_userData, bool _isHalf)
        : outputString(_outputString), aovWrite(_aovWrite), userData(_userData), isHalf(_isHalf)
    {
    }
};

ArnoldAOVTypes GetArnoldTypesFromFormatToken(const TfToken& type);

void ChooseRenderSettings(UsdStageRefPtr stage, std::string &renderSettingsPath, TimeSettings &_time, UsdPrim *rootPrimPtr=nullptr);
void ReadRenderSettings(const UsdPrim &renderSettingsPrim, ArnoldAPIAdapter &context, const TimeSettings &time, AtUniverse *universe, SdfPath& camera);
void ComputeMotionRange(UsdStageRefPtr _stage, const UsdPrim &options,  TimeSettings &_time);

PXR_NAMESPACE_CLOSE_SCOPE