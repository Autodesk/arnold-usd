//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "read_options.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/var.h>

#include <constant_strings.h>
#include <common_utils.h>
#include <parameters_utils.h>
#include <rendersettings_utils.h>
#include "reader/utils.h"
#include "registry.h"
#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/// This function will read the RenderSettings and its dependencies, the linked RenderProduct and RenderVar primitives
AtNode* UsdArnoldReadRenderSettings::Read(const UsdPrim &renderSettingsPrim, UsdArnoldReaderContext &context)
{
    UsdRenderSettings renderSettings(renderSettingsPrim);
    UsdRelationship cameraRel = renderSettings.GetCameraRel();
    SdfPathVector camTargets;
    cameraRel.GetTargets(&camTargets);

    return ReadRenderSettings(renderSettingsPrim, context, context.GetTimeSettings(), context.GetReader()->GetUniverse(), camTargets.empty() ? SdfPath() : camTargets[0]);
}

