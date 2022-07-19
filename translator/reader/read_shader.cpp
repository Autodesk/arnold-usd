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

#include <common_utils.h>
#include <constant_strings.h>

#include "registry.h"
#include "utils.h"
#include "../arnold_usd.h"
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/** Read USD native shaders to Arnold
 *
 **/
void UsdArnoldReadShader::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    std::string nodeName = prim.GetPath().GetText();
    UsdShadeShader shader(prim);
    const TimeSettings &time = context.GetTimeSettings();
    // The "Shader Id" will tell us what is the type of the shader
    TfToken id;
    shader.GetIdAttr().Get(&id, time.frame);
    std::string shaderId = id.GetString();
    if (shaderId.empty())
        return;
    AtNode *node = nullptr;

    // Support shaders having info:id = ArnoldStandardSurface
    if (strncmp(shaderId.c_str(), "Arnold", 6) == 0) {
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
    if (strncmp(shaderId.c_str(), "arnold:", 7) == 0) {
        std::string shaderName = std::string("Arnold_") + shaderId.substr(7);
        shaderName = ArnoldUsdMakeCamelCase(shaderName);
        UsdArnoldPrimReader *primReader = context.GetReader()->GetRegistry()->GetPrimReader(shaderName);
        if (primReader) {
            primReader->Read(prim, context); // read this primitive
        }
        return;
    }

#ifdef ARNOLD_MATERIALX
    // MaterialX shader representing standard surface. In this case we just want to create an arnold standard_surface
    // shader and translate it as is
    if (shaderId == "ND_standard_surface_surfaceshader") {
        UsdArnoldPrimReader *primReader = context.GetReader()->GetRegistry()->GetPrimReader("ArnoldStandardSurface");
        if (primReader) {
            primReader->Read(prim, context);
        }
        return;
    }
    // For arnold-specific shaders in materialx documents, they will show up with the 
    // prefix ARNOLD_ND_ , followed by the arnold node entry name    
    if (strncmp(shaderId.c_str(), "ARNOLD_ND_", 10) == 0) {
        std::string shaderName = std::string("Arnold_") + shaderId.substr(10);
        shaderName = ArnoldUsdMakeCamelCase(shaderName);
        UsdArnoldPrimReader *primReader = context.GetReader()->GetRegistry()->GetPrimReader(shaderName);
        if (primReader) {
            primReader->Read(prim, context); // read this primitive
        }
        return;
    }   

    // Materialx shaders will start with "ND_" in USD
    // We cannot read this for arnold versions up to 7.1.2.x, as the API to get OSL code didn't exist
// #if ARNOLD_VERSION_NUMBER >= 70103
    if (strncmp(shaderId.c_str(), "ND_", 3) == 0) {
        // Create an OSL inline shader
        node = context.CreateArnoldNode("osl", nodeName.c_str());       

        // Get the OSL description of this mtlx shader. Its attributes will be prefixed with 
        // "param_shader_"
        AtString oslCode = AiMaterialxGetOslShaderCode(shaderId.c_str(), "shader");
        // Set the OSL code. This will create a new AtNodeEntry with parameters
        // based on the osl code
        AiNodeSetStr(node, str::code, oslCode);
        // Get the new node entry, after having set the code
        const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
        AiNodeDeclare(node, str::node_def, str::constantString);
        AiNodeSetStr(node, str::node_def, AtString(shaderId.c_str()));

        // Loop over the USD attributes of the shader
        UsdAttributeVector attributes = prim.GetAttributes();
        for (const auto &attribute : attributes) {
            std::string attrName = attribute.GetBaseName().GetString();
            // only consider input attributes
            if (attribute.GetNamespace() != str::t_inputs)
                continue;

            // In order to match the usd attributes with the arnold node attributes, 
            // we need to add the prefix "param_shader_"
            attrName = "param_shader_" + attrName;
            
            const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(attrName.c_str()));
            if (paramEntry == nullptr) {
                // Couldn't find this attribute in the osl entry
                continue;
            }
            uint8_t paramType = AiParamGetType(paramEntry);

            // The tiledimage / image shaders need to create
            // an additional osl shader to represent the filename
            if (paramType == AI_TYPE_POINTER && attrName == "param_shader_file") {
                std::string filename;
                VtValue filenameVal;
                // First, we get the filename attribute value
                if (prim.GetAttribute(str::t_inputs_file).Get(&filenameVal, time.frame))
                    filename = VtValueGetString(filenameVal, &prim);
                // if the filename is empty, there's nothing else to do
                if (!filename.empty()) {
                    // get the metadata "osl_struct" on the arnold attribute for "file", it should be set to "textureresource"
                    AtString fileStr;
                    const static AtString textureSourceStr("textureresource");
                    if (AiMetaDataGetStr(nodeEntry, str::param_shader_file, str::osl_struct, &fileStr) && 
                        fileStr == textureSourceStr)
                    {
                        const static AtString tx_code("struct textureresource { string filename; string colorspace; };\n"
                            "shader texturesource_input(string filename = \"\", string colorspace = \"\", "
                            "output textureresource out = {filename, colorspace}){}");
                        std::string sourceCode = nodeName + std::string("_texturesource");
                        // Create an additional osl shader, for the texture resource. Set it the
                        // hardcoded osl code above
                        AtNode *oslSource = context.CreateArnoldNode("osl", sourceCode.c_str());
                        AiNodeSetStr(oslSource, str::code, tx_code);
                        // Set the actual texture filename to this new osl shader
                        AiNodeSetStr(oslSource, AtString("param_filename"), filename.c_str());
                        // Connect the original osl shader attribute to our new osl shader
                        AiNodeLink(oslSource,"param_shader_file", node);
                        continue;
                    }
                }
            }

            int arrayType = AI_TYPE_NONE;
            if (paramType == AI_TYPE_ARRAY) {
                const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
                // Getting the default array, and checking its type
                arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;
            }
            InputAttribute inputAttr(attribute);
            // Read the attribute value, as we do for regular attributes
            ReadAttribute(prim, inputAttr, node, attrName, time, context, paramType, arrayType);
        }

        
        return;
    }

#endif
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
            paramInput.Get(&specularWorkflow, time.frame);
        }

        if (specularWorkflow == 0) {
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

        // Opacity is a bit complicated as it's a scalar value in USD, but a color in Arnold.
        // So we need to set it properly (see #998)
        AiNodeSetRGB(node, str::opacity, 1.f, 1.f, 1.f);
        UsdShadeInput opacityInput = shader.GetInput(str::t_opacity);
        if (opacityInput) {
            float opacity;
            // if the opacity attribute is linked, we can go through the usual read function
            if (opacityInput.HasConnectedSource()) {
                _ReadBuiltinShaderParameter(shader, node, "opacity", "opacity", context);
            } else if (opacityInput.Get(&opacity, time.frame)) {
                // convert the input float value as RGB in the arnold shader
                AiNodeSetRGB(node, str::opacity, opacity, opacity, opacity);
            }
        }

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
                    uvShader.GetIdAttr().Get(&uvId, time.frame);
                    std::string uvShaderId = uvId.GetString();
                    if (uvShaderId.length() > 18 && uvShaderId.substr(0, 17) == "UsdPrimvarReader_") {
                        // get uvShader attribute inputs:varname and set it as uvset
                        UsdShadeInput varnameInput = uvShader.GetInput(str::t_varname);
                        TfToken varname;
                        if (varnameInput.Get(&varname, time.frame)) {
                            AiNodeSetStr(node, str::uvset, AtString(varname.GetText()));
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
                    const TfToken &usdName, const AtString &arnoldName, float frame) 
        {
            VtValue value;
            UsdShadeInput scaleInput = shader.GetInput(usdName);
            if (scaleInput && scaleInput.Get(&value, frame)) {
                GfVec4f v = VtValueGetVec4f(value);
                AiNodeSetRGB(node, arnoldName, v[0], v[1], v[2]);
            }
        };
        ConvertVec4ToRGB(shader, node, str::t_scale, str::multiply, time.frame);
        ConvertVec4ToRGB(shader, node, str::t_bias, str::offset, time.frame);
        
        auto ConvertWrap = [](UsdShadeShader &shader, AtNode *node, 
                const TfToken &usdName, const AtString &arnoldName, float frame) 
        { 
            VtValue value;
            UsdShadeInput wrapInput = shader.GetInput(usdName);
            if (wrapInput && wrapInput.Get(&value, frame) && value.IsHolding<TfToken>()) {
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
        ConvertWrap(shader, node, str::t_wrapS, str::swrap, time.frame);
        ConvertWrap(shader, node, str::t_wrapT, str::twrap, time.frame);
    } else if (shaderId == "UsdPrimvarReader_float") {
        node = context.CreateArnoldNode("user_data_float", nodeName.c_str());
        _ReadBuiltinShaderParameter(shader, node, "varname", "attribute", context);
        _ReadBuiltinShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_float2") {
        node = context.CreateArnoldNode("user_data_rgb", nodeName.c_str());
        _ReadBuiltinShaderParameter(shader, node, "varname", "attribute", context);
        UsdShadeInput paramInput = shader.GetInput(str::t_fallback);
        GfVec2f vec2Val;
        if (paramInput && paramInput.Get(&vec2Val, time.frame)) {
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
            paramInput.Get(&translation, time.frame);
        
        paramInput = shader.GetInput(str::t_scale);
        if (paramInput) 
            paramInput.Get(&scale, time.frame);
        
        paramInput = shader.GetInput(str::t_rotation);
        if (paramInput)
            paramInput.Get(&rotation, time.frame);
        
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
        shaderName = ArnoldUsdMakeCamelCase(shaderName);
        UsdArnoldPrimReader *primReader = context.GetReader()->GetRegistry()->GetPrimReader(shaderName);
        if (primReader) {
            primReader->Read(prim, context);
        }
    }
    // User-data matrix isn't supported in arnold
    ReadArnoldParameters(prim, context, node, time);
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
        AiMsgWarning("%s", msg.c_str());
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
    ReadAttribute(shader.GetPrim(), inputAttr, node, arnoldAttr, time, context, paramType, arrayType);
}