// Copyright 2019 Autodesk, Inc.
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
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "registry.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *  Export Arnold shaders as UsdShadeShader primitives. The output primitive
 *type is a generic "shader", and the actual shader name will be set in the
 *"info:id" attribute. Input parameter are saved in the "input:" namespace.
 **/

void UsdArnoldWriteShader::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer

    // Create the primitive of type Shader (UsdShadeShader)
    UsdShadeShader shaderAPI = UsdShadeShader::Define(stage, SdfPath(nodeName));
    shaderAPI.CreateIdAttr().Set(TfToken(_usdShaderId)); // set the info:id parameter to the actual shader name

    UsdPrim prim = shaderAPI.GetPrim();
    _WriteArnoldParameters(node, writer, prim, "inputs");
}
