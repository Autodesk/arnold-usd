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
#include "read_shader.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/base/gf/rotation.h>

#include <constant_strings.h>

#include "registry.h"
#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/** Read USD native shaders to Arnold
 *
 **/
void UsdArnoldReadShader::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    std::string nodeName = prim.GetPath().GetText();
    UsdShadeShader shader(prim);
    // The "Shader Id" will tell us what is the type of the shader
    TfToken id;
    shader.GetIdAttr().Get(&id);
    std::string shaderId = id.GetString();
    AtNode *node = nullptr;

    // Support shaders having info:id = ArnoldStandardSurface
    if (shaderId.length() > 6 && shaderId[0] == 'A' && shaderId[1] == 'r' && shaderId[2] == 'n' && shaderId[3] == 'o' &&
        shaderId[4] == 'l' && shaderId[5] == 'd') {
        // We have a USD shader which shaderId is an arnold node name. The
        // result should be equivalent to a custom USD node type with the same
        // name. Let's search in the registry if there is a reader for that type
        UsdArnoldPrimReader *primReader = context.GetReader()->GetRegistry()->GetPrimReader(shaderId);
        if (primReader) {
            primReader->Read(prim, context); // read this primitive
        }
        return;
    }
    // Support shaders having info:id = arnold:standard_surface
    if (shaderId.length() > 7 && shaderId[0] == 'a' && shaderId[1] == 'r' && shaderId[2] == 'n' && shaderId[3] == 'o' &&
        shaderId[4] == 'l' && shaderId[5] == 'd' && shaderId[6] == ':') {
        std::string shaderName = std::string("Arnold_") + shaderId.substr(7);
        shaderName = MakeCamelCase(shaderName);
        UsdArnoldPrimReader *primReader = context.GetReader()->GetRegistry()->GetPrimReader(shaderName);
        if (primReader) {
            primReader->Read(prim, context); // read this primitive
        }
        return;
    }

    if (shaderId == "UsdPreviewSurface") {
        node = context.CreateArnoldNode("standard_surface", nodeName.c_str());

        AiNodeSetRGB(node, str::base_color, 0.18f, 0.18f, 0.18f);
        _ReadBuiltinShaderParameter(shader, node, "diffuseColor", "base_color", context);
        AiNodeSetFlt(node, str::base, 1.f); // scalar multiplier, set it to 1

        AiNodeSetRGB(node, str::emission_color, 0.f, 0.f, 0.f);
        _ReadBuiltinShaderParameter(shader, node, "emissiveColor", "emission_color", context);
        AiNodeSetFlt(node, str::emission, 1.f); // scalar multiplier, set it to 1

        UsdShadeInput paramInput = shader.GetInput(str::t_useSpecularWorkflow);
        int specularWorkflow = 0;
        if (paramInput) {
            paramInput.Get(&specularWorkflow);
        }

        if (specularWorkflow == 1) {
            // metallic workflow, set the specular color to white and use the
            // metalness
            AiNodeSetRGB(node, str::specular_color, 1.f, 1.f, 1.f);
            _ReadBuiltinShaderParameter(shader, node, "metallic", "metalness", context);
        } else {
            AiNodeSetRGB(node, str::specular_color, 1.f, 1.f, 1.f);
            _ReadBuiltinShaderParameter(shader, node, "specularColor", "specular_color", context);
            // this is actually not correct. In USD, this is apparently the
            // fresnel 0° "front-facing" specular color. Specular is considered
            // to be always white for grazing angles
        }

        AiNodeSetFlt(node, str::specular_roughness, 0.5);
        _ReadBuiltinShaderParameter(shader, node, "roughness", "specular_roughness", context);

        AiNodeSetFlt(node, str::specular_IOR, 1.5);
        _ReadBuiltinShaderParameter(shader, node, "ior", "specular_IOR", context);

        AiNodeSetFlt(node, str::coat, 0.f);
        _ReadBuiltinShaderParameter(shader, node, "clearcoat", "coat", context);

        AiNodeSetFlt(node, str::coat_roughness, 0.01f);
        _ReadBuiltinShaderParameter(shader, node, "clearcoatRoughness", "coat_roughness", context);

        AiNodeSetRGB(node, str::opacity, 1.f, 1.f, 1.f);
        _ReadBuiltinShaderParameter(shader, node, "opacity", "opacity", context);

        UsdShadeInput normalInput = shader.GetInput(str::t_normal);
        if (normalInput && normalInput.HasConnectedSource()) {
            // Usd expects a tangent normal map, let's create a normal_map
            // shader, and connect it there
            std::string normalMapName = nodeName + "@normal_map";
            AtNode *normalMap = context.CreateArnoldNode("normal_map", normalMapName.c_str());
            AiNodeSetBool(normalMap, str::color_to_signed, false);
            _ReadBuiltinShaderParameter(shader, normalMap, "normal", "input", context);
            AiNodeLink(normalMap, "normal", node);
        }
        // We're not exporting displacement (float) as it's part of meshes in
        // arnold. We're also not exporting the occlusion parameter (float),
        // since it doesn't really apply for arnold.

    } else if (shaderId == "UsdUVTexture") {
        node = context.CreateArnoldNode("image", nodeName.c_str());

        // Texture Shader, we want to export it as arnold "image" node
        _ReadBuiltinShaderParameter(shader, node, "file", "filename", context);

        bool exportSt = true;
        UsdShadeInput uvCoordInput = shader.GetInput(str::t_st);
        if (uvCoordInput) {
            SdfPathVector sourcePaths;
            // First check if there's a connection to this input attribute
            if (uvCoordInput.HasConnectedSource() && uvCoordInput.GetRawConnectedSourcePaths(&sourcePaths) &&
                !sourcePaths.empty()) {
                UsdPrim uvPrim = context.GetReader()->GetStage()->GetPrimAtPath(sourcePaths[0].GetPrimPath());
                UsdShadeShader uvShader = (uvPrim) ? UsdShadeShader(uvPrim) : UsdShadeShader();
                if (uvShader) {
                    TfToken uvId;
                    uvShader.GetIdAttr().Get(&uvId);
                    std::string uvShaderId = uvId.GetString();
                    if (uvShaderId.length() > 18 && uvShaderId.substr(0, 17) == "UsdPrimvarReader_") {
                        // get uvShader attribute inputs:varname and set it as uvset
                        UsdShadeInput varnameInput = uvShader.GetInput(str::t_varname);
                        TfToken varname;
                        if (varnameInput.Get(&varname)) {
                            AiNodeSetStr(node, str::uvset, varname.GetText());
                            exportSt = false;
                        }
                    }
                }
            }
        }

        // In USD, meshes don't have a "default" UV set. So we always need to
        // connect it to a user data shader.
        if (exportSt) {
            _ReadBuiltinShaderParameter(shader, node, "st", "uvcoords", context);
        }
        _ReadBuiltinShaderParameter(shader, node, "fallback", "missing_texture_color", context);

        auto ConvertVec4ToRGB = [](UsdShadeShader &shader, AtNode *node, 
                    const TfToken &usdName, const AtString &arnoldName) 
        {
            VtValue value;
            UsdShadeInput scaleInput = shader.GetInput(usdName);
            if (scaleInput && scaleInput.Get(&value)) {
                GfVec4f v = VtValueGetVec4f(value);
                AiNodeSetRGB(node, arnoldName, v[0], v[1], v[2]);
            }
        };
        ConvertVec4ToRGB(shader, node, str::t_scale, str::multiply);
        ConvertVec4ToRGB(shader, node, str::t_bias, str::offset);
        
        auto ConvertWrap = [](UsdShadeShader &shader, AtNode *node, 
                const TfToken &usdName, const AtString &arnoldName) 
        { 
            VtValue value;
            UsdShadeInput wrapInput = shader.GetInput(usdName);
            if (wrapInput && wrapInput.Get(&value) && value.IsHolding<TfToken>()) {
                TfToken wrap = value.UncheckedGet<TfToken>();
                if (wrap == str::t_repeat)
                    AiNodeSetStr(node, arnoldName, str::periodic);
                else if (wrap == str::t_mirror)
                    AiNodeSetStr(node, arnoldName, str::mirror);
                else if (wrap == str::t_clamp)
                    AiNodeSetStr(node, arnoldName, str::clamp);
                else if (wrap == str::t_black)
                    AiNodeSetStr(node, arnoldName, str::black);
                else // default is useMetadata
                    AiNodeSetStr(node, arnoldName, str::file);                
            } else 
                AiNodeSetStr(node, arnoldName, str::file);
        };
        ConvertWrap(shader, node, str::t_wrapS, str::swrap);
        ConvertWrap(shader, node, str::t_wrapT, str::twrap);
    } else if (shaderId == "UsdPrimvarReader_float") {
        node = context.CreateArnoldNode("user_data_float", nodeName.c_str());
        _ReadBuiltinShaderParameter(shader, node, "varname", "attribute", context);
        _ReadBuiltinShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_float2") {
        node = context.CreateArnoldNode("user_data_rgb", nodeName.c_str());
        _ReadBuiltinShaderParameter(shader, node, "varname", "attribute", context);
        UsdShadeInput paramInput = shader.GetInput(str::t_fallback);
        GfVec2f vec2Val;
        if (paramInput && paramInput.Get(&vec2Val)) {
            AiNodeSetRGB(node, str::_default, vec2Val[0], vec2Val[1], 0.f);
        }
    } else if (
        shaderId == "UsdPrimvarReader_float3" || shaderId == "UsdPrimvarReader_normal" ||
        shaderId == "UsdPrimvarReader_point" || shaderId == "UsdPrimvarReader_vector") {
        node = context.CreateArnoldNode("user_data_rgb", nodeName.c_str());
        _ReadBuiltinShaderParameter(shader, node, "varname", "attribute", context);
        _ReadBuiltinShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_float4") {
        node = context.CreateArnoldNode("user_data_rgba", nodeName.c_str());
        _ReadBuiltinShaderParameter(shader, node, "varname", "attribute", context);
        _ReadBuiltinShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_int") {
        node = context.CreateArnoldNode("user_data_int", nodeName.c_str());
        _ReadBuiltinShaderParameter(shader, node, "varname", "attribute", context);
        _ReadBuiltinShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_string") {
        node = context.CreateArnoldNode("user_data_string", nodeName.c_str());
        _ReadBuiltinShaderParameter(shader, node, "varname", "attribute", context);
        _ReadBuiltinShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdTransform2d") {
        node = context.CreateArnoldNode("matrix_multiply_vector", nodeName.c_str());
        _ReadBuiltinShaderParameter(shader, node, "in", "input", context);
        GfVec2f translation = GfVec2f(0.f, 0.f);
        GfVec2f scale = GfVec2f(1.f, 1.f);
        float rotation = 0.f;

        UsdShadeInput paramInput = shader.GetInput(str::t_translation);
        if (paramInput) 
            paramInput.Get(&translation);
        
        paramInput = shader.GetInput(str::t_scale);
        if (paramInput) 
            paramInput.Get(&scale);
        
        paramInput = shader.GetInput(str::t_rotation);
        if (paramInput)
            paramInput.Get(&rotation);
        
        GfMatrix4f texCoordTransfromMatrix(1.0);
        GfMatrix4f m;
        m.SetScale({scale[0], scale[1], 1.0f});
        texCoordTransfromMatrix *= m;
    
        m.SetRotate(GfRotation(GfVec3d(0.0, 0.0, 1.0), rotation));
        texCoordTransfromMatrix *= m;
        
        m.SetTranslate({translation[0], translation[1], 0.0f});
        texCoordTransfromMatrix *= m;
        
        AtMatrix matrix;
        const float* array = texCoordTransfromMatrix.GetArray();
        memcpy(&matrix.data[0][0], array, 16 * sizeof(float));
        AiNodeSetMatrix(node, str::matrix, matrix);
    } else {
        // support info:id = standard_surface
        std::string shaderName = std::string("Arnold_") + shaderId;
        shaderName = MakeCamelCase(shaderName);
        UsdArnoldPrimReader *primReader = context.GetReader()->GetRegistry()->GetPrimReader(shaderName);
        if (primReader) {
            primReader->Read(prim, context);
        }
    }
    // User-data matrix isn't supported in arnold
    const TimeSettings &time = context.GetTimeSettings();
    _ReadArnoldParameters(prim, context, node, time);
}

void UsdArnoldReadShader::_ReadBuiltinShaderParameter(
    UsdShadeShader &shader, AtNode *node, const std::string &usdAttr, const std::string &arnoldAttr,
    UsdArnoldReaderContext &context)
{
    if (node == nullptr)
        return;

    const TimeSettings &time = context.GetTimeSettings();

    const AtNodeEntry *nentry = AiNodeGetNodeEntry(node);
    const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nentry, AtString(arnoldAttr.c_str()));
    int paramType = AiParamGetType(paramEntry);

    if (nentry == nullptr || paramEntry == nullptr) {
        std::string msg = "Couldn't find attribute ";
        msg += arnoldAttr;
        msg += " from node ";
        msg += AiNodeGetName(node);
        AiMsgWarning(msg.c_str());
        return;
    }

    int arrayType = AI_TYPE_NONE;
    if (paramType == AI_TYPE_ARRAY) {
        const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
        // Getting the default array, and checking its type
        arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;
    }

    UsdShadeInput paramInput = shader.GetInput(TfToken(usdAttr.c_str()));
    if (!paramInput)
        return;

    const UsdAttribute &attr = paramInput.GetAttr();
    InputAttribute inputAttr(attr);
    ReadAttribute(inputAttr, node, arnoldAttr, time, context, paramType, arrayType);
}