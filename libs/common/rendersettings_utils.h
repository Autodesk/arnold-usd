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

void ChooseRenderSettings(UsdStageRefPtr stage, std::string &renderSettingsPath, TimeSettings &_time, UsdPrim *rootPrimPtr=nullptr);
void ReadRenderSettings(const UsdPrim &renderSettingsPrim, ArnoldAPIAdapter &context, const TimeSettings &time, AtUniverse *universe);
void ComputeMotionRange(UsdStageRefPtr _stage, const UsdPrim &options,  TimeSettings &_time);

PXR_NAMESPACE_CLOSE_SCOPE