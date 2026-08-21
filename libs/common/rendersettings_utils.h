//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once
#include <string>
#include <vector>

#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>
#include <ai.h>
#include "timesettings.h"
#include "api_adapter.h"
#include "procedural_reader.h"

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
AtNode* ReadRenderSettings(const UsdPrim &renderSettingsPrim, ArnoldAPIAdapter &context, ProceduralReader *reader, const TimeSettings &time, AtUniverse *universe, SdfPath& camera);
void ComputeMotionRange(UsdStageRefPtr _stage, const UsdPrim &options,  TimeSettings &_time);
void ComputeUsdLuxVersion(UsdStageRefPtr _stage, const UsdPrim &options,  TimeSettings &_time, const AtUniverse *universe);
void SetArnoldDefaultOptions(AtUniverse *universe);
void SetRegion(AtNode* options, const GfVec4f& windowNDC, const GfVec2i& resolution);

// Resolve the driver_exr.compression applying to layer #index of an exr driver, based on whatever
// is currently set on the node (i.e. the RenderProduct-level value, if any). This mirrors arnold's
// own positional rule: element #index, else element 0, else "zip" (see driver_open in
// driver_exr.cpp).
std::string GetDriverCompressionFallback(const AtNode *driver, size_t index);

// Set the positional per-layer compression array on a driver_exr node, gathered from the
// arnold:driver_exr:compression attribute of each RenderVar. Note that per-RenderVar settings are
// usually named arnold:{parameter} (e.g. arnold:layer_tolerance), but "compression" is an existing
// RenderProduct-level parameter, so we use the fully qualified arnold:driver_exr:compression on
// RenderVars as well, to make it obvious that both refer to the same driver parameter.
//
// compressions must be index-aligned with the outputs this driver receives. An empty entry means
// "this RenderVar didn't author a compression" and is filled with GetDriverCompressionFallback,
// so as soon as one RenderVar authors a compression, every layer ends up with an explicit value.
// If no entry was authored, or if the node isn't a driver_exr, this is a no-op, so that a
// compression authored on the RenderProduct is left untouched. Callers that reuse their driver
// nodes across updates are responsible for resetting the parameter beforehand, see
// HdArnoldRenderPass::_Execute.
void SetDriverExrCompressions(AtNode *driver, const std::vector<std::string> &compressions);

// Color manager helper functions
AtNode* GetOrCreateColorManager(const UsdPrim &renderSettingsPrim, ArnoldAPIAdapter &context, 
                                 const TimeSettings &time, AtNode *options);
void SetupColorManagerColorSpaces(AtNode *colorManager, const UsdPrim &renderSettingsPrim, 
                                   ArnoldAPIAdapter &context, const TimeSettings &time);

PXR_NAMESPACE_CLOSE_SCOPE