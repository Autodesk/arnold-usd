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
#include "write_shader.h"

#include <ai.h>

#include <pxr/base/tf/token.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "registry.h"
#include <constant_strings.h>
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *  Export Arnold shaders as UsdShadeShader primitives. The output primitive
 *type is a generic "shader", and the actual shader name will be set in the
 *"info:id" attribute. Input parameter are saved in the "input:" namespace.
 **/

void UsdArnoldWriteShader::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    UsdShadeShader shaderAPI = UsdShadeShader::Define(writer.GetUsdStage(), SdfPath(GetArnoldNodeName(node, writer)));
    // set the info:id parameter to the actual shader name
    writer.SetAttribute(shaderAPI.CreateIdAttr(), TfToken(_usdShaderId));
    UsdPrim prim = shaderAPI.GetPrim();
    _WriteArnoldParameters(node, writer, prim, "inputs");
    // Special case for image nodes, we want to set an attribute to force the Arnold way of handling relative paths
    if (_usdShaderId == str::t_arnold_image) {
        AtString filenameStr = AiNodeGetStr(node, str::filename);
        if (TfIsRelativePath(std::string(filenameStr.c_str()))) {
            UsdAttribute filenameAttr = shaderAPI.GetInput(str::t_filename);
            if (filenameAttr)
                filenameAttr.SetCustomDataByKey(str::t_arnold_relative_path, VtValue(true));
        }
    }
}
