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

#if ARNOLD_VERSION_NUM >= 70301
    // For imagers, we need to treat the input attribute in a particular way
    if (AiNodeEntryGetType(AiNodeGetNodeEntry(node)) == AI_NODE_IMAGER) {
        AtNode* inputImager = (AtNode*)AiNodeGetPtr(node, str::input);
        if (inputImager) {
            writer.WritePrimitive(inputImager);
            std::string inputImagerName = UsdArnoldPrimWriter::GetArnoldNodeName(inputImager, writer);
            if (!inputImagerName.empty()) {
                UsdPrim inputImagerPrim = writer.GetUsdStage()->GetPrimAtPath(SdfPath(inputImagerName));
                if (inputImagerPrim) {
                    UsdAttribute arnoldInputAttr = 
                        prim.CreateAttribute(TfToken("inputs:input"), SdfValueTypeNames->String, false);

                    SdfPath imagerOutput(inputImagerName + std::string(".outputs:out"));
                    arnoldInputAttr.AddConnection(imagerOutput);
                }
            }
        }
        _exportedAttrs.insert("input");
    }
#endif
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
    // Ensure all shaders have an output attribute
    prim.CreateAttribute(str::t_outputs_out, SdfValueTypeNames->Token, false);

    if (!(writer.GetMask() & AI_NODE_SHAPE)) {
        // If shapes are not exported and a material is specified for this shader, let's create it
        AtString materialName;
        bool isDisplacement = false;
        // material_surface / material_displacement / material_volume are user data 
        // authored by the arnold plugins when a shader library is exported, so that 
        // materials can be restored at import. We can use this to create the
        // USD material primitives #2047
        if (AiNodeLookUpUserParameter(node, str::material_surface)) {
            materialName = AiNodeGetStr(node, str::material_surface);
        } else if (AiNodeLookUpUserParameter(node, str::material_displacement)) {
            materialName = AiNodeGetStr(node, str::material_displacement);
            isDisplacement = true;
        } else if (AiNodeLookUpUserParameter(node, str::material_volume)) {
            // Note that volume assignments are treated the same way as
            // surface shader assignments in our usd support
            materialName = AiNodeGetStr(node, str::material_volume);
        }

        if (!materialName.empty()) {
            std::string matName(materialName.c_str());
            _SanitizePrimName(matName);
            std::string mtlScope = writer.GetScope() + writer.GetMtlScope();
            writer.CreateScopeHierarchy(SdfPath(mtlScope));
            matName = mtlScope + matName;
            UsdShadeMaterial mat = UsdShadeMaterial::Define(writer.GetUsdStage(), SdfPath(matName));
            const TfToken arnoldContext("arnold");
            std::string shaderOutput = prim.GetPath().GetString() + std::string(".outputs:out");
            UsdShadeOutput matOutput = (isDisplacement) ? 
                mat.CreateDisplacementOutput(arnoldContext) : 
                mat.CreateSurfaceOutput(arnoldContext);

            matOutput.ConnectToSource(SdfPath(shaderOutput));
        }
    }
}
