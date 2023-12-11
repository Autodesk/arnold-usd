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
#include "read_shader.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/utils.h>
#include <pxr/base/gf/rotation.h>

#include <common_utils.h>
#include <constant_strings.h>
#include <parameters_utils.h>

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
    } else if (strncmp(shaderId.c_str(), "arnold:", 7) == 0) {
        // Support shaders having info:id = arnold:standard_surface
        std::string shaderName = shaderId.substr(7);
        if (AiNodeEntryLookUp(AtString(shaderName.c_str()))) {
            node = context.CreateArnoldNode(shaderName.c_str(), prim.GetPath().GetText());
            ReadShaderInputs(prim, context, node);
        } 
    } else if (shaderId == "UsdPreviewSurface") {
        node = context.CreateArnoldNode("standard_surface", nodeName.c_str());

        AiNodeSetRGB(node, str::base_color, 0.18f, 0.18f, 0.18f);
        _ReadShaderParameter(shader, node, "diffuseColor", "base_color", context);
        AiNodeSetFlt(node, str::base, 1.f); // scalar multiplier, set it to 1

        AiNodeSetRGB(node, str::emission_color, 0.f, 0.f, 0.f);
        _ReadShaderParameter(shader, node, "emissiveColor", "emission_color", context);
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
            _ReadShaderParameter(shader, node, "metallic", "metalness", context);
        } else {
            AiNodeSetRGB(node, str::specular_color, 1.f, 1.f, 1.f);
            _ReadShaderParameter(shader, node, "specularColor", "specular_color", context);
            // this is actually not correct. In USD, this is apparently the
            // fresnel 0° "front-facing" specular color. Specular is considered
            // to be always white for grazing angles
        }

        AiNodeSetFlt(node, str::specular_roughness, 0.5);
        _ReadShaderParameter(shader, node, "roughness", "specular_roughness", context);

        AiNodeSetFlt(node, str::specular_IOR, 1.5);
        _ReadShaderParameter(shader, node, "ior", "specular_IOR", context);

        AiNodeSetFlt(node, str::coat, 0.f);
        _ReadShaderParameter(shader, node, "clearcoat", "coat", context);

        AiNodeSetFlt(node, str::coat_roughness, 0.01f);
        _ReadShaderParameter(shader, node, "clearcoatRoughness", "coat_roughness", context);

        // Opacity is a bit complicated as it's a scalar value in USD, but a color in Arnold.
        // So we need to set it properly (see #998)
        // Arnold RGB           opacity       1, 1, 1
        // Arnold FLOAT         transmission  0
        // USD    FLOAT         opacity
        //AiNodeSetRGB(node, str::opacity, 1.f, 1.f, 1.f);
        UsdShadeInput opacityInput = shader.GetInput(str::t_opacity);
        if (opacityInput) {
            // if the opacity attribute is linked, we can go through the usual read function
            const std::string subtractNodeName = nodeName + "@subtract";
            AtNode *subtractNode = context.CreateArnoldNode("subtract", subtractNodeName.c_str());
            AiNodeSetRGB(subtractNode, str::input1, 1.f, 1.f, 1.f);
            float opacity;
            if (opacityInput.HasConnectedSource()) {
                _ReadShaderParameter(shader, subtractNode, "opacity", "input2", context);
            } else if (opacityInput.Get(&opacity, time.frame)) {
                // convert the input float value as RGB in the arnold shader
                AiNodeSetRGB(subtractNode, str::input2, opacity, opacity, opacity);
            }
            AiNodeLink(subtractNode, "transmission", node);
        }

        UsdShadeInput normalInput = shader.GetInput(str::t_normal);
        if (normalInput && normalInput.HasConnectedSource()) {
            // Usd expects a tangent normal map, let's create a normal_map
            // shader, and connect it there
            std::string normalMapName = nodeName + "@normal_map";
            AtNode *normalMap = context.CreateArnoldNode("normal_map", normalMapName.c_str());
            AiNodeSetBool(normalMap, str::color_to_signed, false);
            _ReadShaderParameter(shader, normalMap, "normal", "input", context);
            AiNodeLink(normalMap, "normal", node);
        }
        // We're not exporting displacement (float) as it's part of meshes in
        // arnold. We're also not exporting the occlusion parameter (float),
        // since it doesn't really apply for arnold.

    } else if (shaderId == "UsdUVTexture") {
        node = context.CreateArnoldNode("image", nodeName.c_str());

        UsdShadeInput fileInput = shader.GetInput(str::t_file);
        std::string filenameStr;
        if (fileInput) {
            UsdAttribute fileInputAttr = fileInput.GetAttr();
            VtValue fileInputValue;
            if (fileInputAttr.Get(&fileInputValue, time.frame)) {
                SdfAssetPath assetPath;                
                if (fileInputValue.IsHolding<SdfAssetPath>()) {
                    assetPath = fileInputValue.UncheckedGet<SdfAssetPath>();
                } else if (fileInputValue.IsHolding<VtArray<SdfAssetPath>>()) {
                    const auto& pathArray = fileInputValue.UncheckedGet<VtArray<SdfAssetPath>>();
                    if (!pathArray.empty())
                        assetPath = pathArray[0];
                } else {
                    filenameStr = VtValueGetString(fileInputValue);
                }
                if (filenameStr.empty()) {
                    filenameStr = assetPath.GetResolvedPath();
                    if (filenameStr.empty()) {
                        filenameStr = assetPath.GetAssetPath();
                        if (!filenameStr.empty()) {
                            // SdfComputeAssetPathRelativeToLayer returns search paths (vs anchored paths) unmodified,
                            // this is apparently to make sure they will be always searched again.
                            // This is not what we want, so we make sure the path is anchored
                            if (TfIsRelativePath(filenameStr) && filenameStr[0] != '.') {
                                filenameStr = "./" + filenameStr;
                            }
                            for (const auto& sdfProp : fileInputAttr.GetPropertyStack()) {
                                const auto& layer = sdfProp->GetLayer();
                                if (layer && !layer->GetRealPath().empty()) {
                                    std::string layerPath = SdfComputeAssetPathRelativeToLayer(layer, filenameStr);
                                    if (!layerPath.empty() && layerPath != filenameStr &&
                                        TfPathExists(layerPath.substr(0, layerPath.find_last_of("\\/")))) {
                                        filenameStr = layerPath;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        AiNodeSetStr(node, str::filename, AtString(filenameStr.c_str()));

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
                        // From version 2.3 of the USD Preview Surface Proposal
                        // varname input type is changed from token to string. We check both as we can be
                        // reading files authored with old usd versions
                        UsdShadeInput varnameInput = uvShader.GetInput(str::t_varname);
                        if (varnameInput) {
                            UsdAttribute varNameAttr = varnameInput.GetAttr();
#if PXR_VERSION > 2011
                            UsdShadeAttributeVector resolvedAttrs = 
                                UsdShadeUtils::GetValueProducingAttributes(varnameInput);
                            if (!resolvedAttrs.empty())
                                varNameAttr = resolvedAttrs[0];
#endif

                            VtValue varnameValue;
                            if (varNameAttr.Get(&varnameValue, time.frame)) {
                                // If the var name is "st", then this primvar will have been converted 
                                // to the geometry's main uv set, so we don't need to set the 
                                // image uvset parameter
                                std::string varnameStr = VtValueGetString(varnameValue);
                                if (varnameStr != "st")
                                    AiNodeSetStr(node, str::uvset, AtString(varnameStr.c_str()));
                                exportSt = false;
                            }
                        }
                    }
                }
            }
        }

        // In USD, meshes don't have a "default" UV set. So we always need to
        // connect it to a user data shader.
        if (exportSt) {
            _ReadShaderParameter(shader, node, "st", "uvcoords", context);
        }
        _ReadShaderParameter(shader, node, "fallback", "missing_texture_color", context);
        // To be consistent with USD, we ignore the missing textures
        AiNodeSetBool(node, str::ignore_missing_textures, true);

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
        _ReadShaderParameter(shader, node, "varname", "attribute", context);
        _ReadShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_float2") {
        // If the user data attribute name is "st" or "uv", this actually
        // means that we should be looking at the builtin uv coordinates. 
        UsdShadeInput varNameInput = shader.GetInput(str::t_varname);
        std::string varName;
        if (varNameInput) {
            VtValue varNameValue;
#if PXR_VERSION > 2011
            UsdShadeAttributeVector resolvedAttrs = 
                UsdShadeUtils::GetValueProducingAttributes(varNameInput);
            if (!resolvedAttrs.empty() && resolvedAttrs[0].Get(&varNameValue, time.frame)) {
                varName = VtValueGetString(varNameValue);
            }
#else
            UsdAttribute varNameAttr = varNameInput.GetAttr();
            if (varNameAttr.Get(&varNameValue, time.frame)) {
                varName = VtValueGetString(varNameValue);
            }
#endif
        }
        if (varName != "st" && varName != "uv") {
            // Create a user_data shader that will lookup the user data (primvar)
            // and return its value
            node = context.CreateArnoldNode("user_data_rgb", nodeName.c_str());
            AiNodeSetStr(node, str::attribute, AtString(varName.c_str()));
            UsdShadeInput paramInput = shader.GetInput(str::t_fallback);
            GfVec2f vec2Val;
            if (paramInput && paramInput.Get(&vec2Val, time.frame)) {
                AiNodeSetRGB(node, str::_default, vec2Val[0], vec2Val[1], 0.f);
            }
        } else {
            // For "st" and "uv" the user_data shader won't help and instead we want to 
            // create a utility shader returning the uvs
            node = context.CreateArnoldNode("utility", nodeName.c_str());
            AiNodeSetStr(node, str::shade_mode, str::flat);
            AiNodeSetStr(node, str::color_mode, str::uv);
        }
    } else if (
        shaderId == "UsdPrimvarReader_float3" || shaderId == "UsdPrimvarReader_normal" ||
        shaderId == "UsdPrimvarReader_point" || shaderId == "UsdPrimvarReader_vector") {
        node = context.CreateArnoldNode("user_data_rgb", nodeName.c_str());
        _ReadShaderParameter(shader, node, "varname", "attribute", context);
        _ReadShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_float4") {
        node = context.CreateArnoldNode("user_data_rgba", nodeName.c_str());
        _ReadShaderParameter(shader, node, "varname", "attribute", context);
        _ReadShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_int") {
        node = context.CreateArnoldNode("user_data_int", nodeName.c_str());
        _ReadShaderParameter(shader, node, "varname", "attribute", context);
        _ReadShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_string") {
        node = context.CreateArnoldNode("user_data_string", nodeName.c_str());
        _ReadShaderParameter(shader, node, "varname", "attribute", context);
        _ReadShaderParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdTransform2d") {
        node = context.CreateArnoldNode("matrix_multiply_vector", nodeName.c_str());
        _ReadShaderParameter(shader, node, "in", "input", context);
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
        // Default case. We ask Arnold if this shader corresponds to a
        // materialx node definition
        // if a custom USD Materialx path is set, we need to provide it to 
        // Arnold's Materialx lib so that it can find custom node definitions
        AtParamValueMap *params = AiParamValueMap();
        const AtString &pxrMtlxPath = context.GetReader()->GetPxrMtlxPath();
        if (!pxrMtlxPath.empty()) {
            AiParamValueMapSetStr(params, str::MATERIALX_NODE_DEFINITIONS, pxrMtlxPath);
        }

#if ARNOLD_VERSION_NUM > 70203
        const AtNodeEntry* shaderNodeEntry = AiMaterialxGetNodeEntryFromDefinition(shaderId.c_str(), params);
#else
        // arnold backwards compatibility. We used to rely on the nodedef prefix to identify 
        // the shader type
        AtString shaderEntryStr;
        if (shaderId == "ND_standard_surface_surfaceshader")
            shaderEntryStr = str::standard_surface;
        else if (strncmp(shaderId.c_str(), "ND_", 3) == 0)
            shaderEntryStr = str::osl;
        else if (strncmp(shaderId.c_str(), "ARNOLD_ND_", 10) == 0)
            shaderEntryStr = AtString(shaderId.c_str() + 10);

        const AtNodeEntry *shaderNodeEntry = shaderEntryStr.empty() ? 
            nullptr : AiNodeEntryLookUp(shaderEntryStr);
#endif
        if (shaderNodeEntry) {
            std::string shaderNodeEntryName = AiNodeEntryGetName(shaderNodeEntry);
            if (shaderNodeEntryName == "osl") {
                // There is an OSL description for this materialx shader. 
                // Its attributes will be prefixed with "param_shader_"
                UsdAttributeVector attributes = prim.GetAttributes();

                AtString oslCode;
                // The "params" argument was added to AiMaterialxGetOslShader in 7.2.0.0
#if ARNOLD_VERSION_NUM > 70104
                for (const auto &attribute : attributes) {
                    if(attribute.HasAuthoredConnections()) {
                        // Only the key is used, so we set an empty string for the value
                        AiParamValueMapSetStr(params, AtString(attribute.GetBaseName().GetString().c_str()), AtString(""));
                    }
                }
                oslCode = AiMaterialxGetOslShaderCode(shaderId.c_str(), "shader", params);
#elif ARNOLD_VERSION_NUM >= 70104
                oslCode = AiMaterialxGetOslShaderCode(shaderId.c_str(), "shader");
#endif

                if (!oslCode.empty()) {
                    // Create an OSL inline shader
                    node = context.CreateArnoldNode("osl", nodeName.c_str());       
                    // Set the OSL code. This will create a new AtNodeEntry with parameters
                    // based on the osl code
                    AiNodeSetStr(node, str::code, oslCode);

                    // Get the new node entry, after having set the code
                    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
                    AiNodeDeclare(node, str::node_def, str::constantString);
                    AiNodeSetStr(node, str::node_def, AtString(shaderId.c_str()));

                    // Loop over the USD attributes of the shader
                    for (const auto &attribute : attributes) {
                        // only consider input attributes
                        if (attribute.GetNamespace() != str::t_inputs)
                            continue;

                        // In order to match the usd attributes with the arnold node attributes, 
                        // we need to add the prefix "param_shader_"
                        std::string attrName = "param_shader_" + attribute.GetBaseName().GetString();
                        AtString paramName(attrName.c_str()); 
                        const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nodeEntry, paramName);

                        if (paramEntry == nullptr) {
                            // If we failed to find the attribute, try without the shader prefix
                            // this is needed for non editable (BSDF/EDF/VDF) MaterialX node inputs
                            attrName = "param_" + attribute.GetBaseName().GetString();
                            paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(attrName.c_str()));
                            if (paramEntry == nullptr) {
                                // Couldn't find this attribute in the osl entry
                                continue;
                            }
                        }
                        uint8_t paramType = AiParamGetType(paramEntry);

                        // The tiledimage / image shaders need to create
                        // an additional osl shader to represent the filename
                        if (paramType == AI_TYPE_POINTER && TfStringStartsWith(attrName, "param_shader_file")) {
                            std::string filename;
                            VtValue filenameVal;
                            if (attribute.Get(&filenameVal, time.frame)) {
                                filename = VtValueGetString(filenameVal);  
                            }
                            // if the filename is empty, there's nothing else to do
                            if (!filename.empty()) {
                                // get the metadata "osl_struct" on the arnold attribute for "file", it should be set to "textureresource"
                                AtString fileStr;
                                const static AtString textureSourceStr("textureresource");
                                if (AiMetaDataGetStr(nodeEntry, paramName, str::osl_struct, &fileStr) && 
                                    fileStr == textureSourceStr)
                                {
                                    const static AtString tx_code("struct textureresource { string filename; string colorspace; };\n"
                                        "shader texturesource_input(string filename = \"\", string colorspace = \"\", "
                                        "output textureresource out = {filename, colorspace}){}");
                                    std::string sourceCode = nodeName + std::string("_texturesource_") + attribute.GetBaseName().GetString();
                                    // Create an additional osl shader, for the texture resource. Set it the
                                    // hardcoded osl code above
                                    AtNode *oslSource = context.CreateArnoldNode("osl", sourceCode.c_str());
                                    AiNodeSetStr(oslSource, str::code, tx_code);
                                    // Set the actual texture filename to this new osl shader
                                    AiNodeSetStr(oslSource, str::param_filename, AtString(filename.c_str()));

                                    // Check if this "file" attribute has a colorSpace metadata 
                                    if (attribute.HasMetadata(TfToken("colorSpace"))) {
                                        // if a metadata is present, set this value in the OSL shader
                                        VtValue colorSpaceValue;
                                        attribute.GetMetadata(TfToken("colorSpace"), &colorSpaceValue);
                                        std::string colorSpaceStr = VtValueGetString(colorSpaceValue);
                                        AiNodeSetStr(oslSource, str::param_colorspace, AtString(colorSpaceStr.c_str()));
                                    } else {
                                        // no metadata is present, rely on the default "auto"
                                        AiNodeSetStr(oslSource, str::param_colorspace, str::_auto);
                                    }
                                    // Connect the original osl shader attribute to our new osl shader
                                    AiNodeLink(oslSource,paramName, node);
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
                        InputUsdAttribute inputAttr(attribute);
                        // Read the attribute value, as we do for regular attributes
                        ReadAttribute(inputAttr, node, attrName, time, context, paramType, arrayType);
                    }
                }
            } else {

                // This materialx node definition corresponds to a builtin arnold shader 
                // (standard_surface, etc...).
                // In this case we want to read the shader as if it was defined through a regular 
                // Usd Shader.
                std::string shaderName = std::string("Arnold_") + shaderNodeEntryName;
                shaderName = ArnoldUsdMakeCamelCase(shaderName);
                UsdArnoldPrimReader *primReader = context.GetReader()->GetRegistry()->GetPrimReader(shaderName);
                if (primReader) {
                    primReader->Read(prim, context);
                }
            }
        }
        AiParamValueMapDestroy(params);
    }

}

void UsdArnoldReadShader::_ReadShaderParameter(
    UsdShadeShader &shader, AtNode *node, const std::string &usdAttr, const std::string &arnoldAttr,
    UsdArnoldReaderContext &context)
{
    if (node == nullptr)
        return;

    UsdShadeInput paramInput = shader.GetInput(TfToken(usdAttr.c_str()));
    if (!paramInput)
        return;

    _ReadShaderInput(paramInput, node, arnoldAttr, context);
}
void UsdArnoldReadShader::_ReadShaderInput(const UsdShadeInput& input, AtNode* node, 
    const std::string& arnoldAttr, UsdArnoldReaderContext& context)
{
    const AtNodeEntry *nentry = node ? AiNodeGetNodeEntry(node) : nullptr;
    if (nentry == nullptr) {
        return;
    }
    UsdAttribute attr = input.GetAttr();
    bool hasConnection = attr.HasAuthoredConnections();
    
    if(hasConnection) {
        TfToken inputName = input.GetBaseName();
        if (inputName != attr.GetBaseName()) {
            // Linked array attributes : This isn't supported natively in USD, so we need
            // to read it in a specific format. If attribute "attr" has element 1 linked to
            // a shader, we will write it as attr:i1
            size_t indexPos = inputName.GetString().find(":i");
            if (indexPos != std::string::npos) {
                UsdPrim prim = attr.GetPrim();
                ReadArrayLink(prim, attr, context.GetTimeSettings(), context, node, str::t_inputs);
                return;
            }
        }
    }
    
    const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nentry, AtString(arnoldAttr.c_str()));
    int paramType = AiParamGetType(paramEntry);

    if (paramEntry == nullptr) {
        TfToken inputName = input.GetFullName();
        AiMsgWarning(
            "USD arnold attribute %s not recognized in %s for %s", 
            inputName.GetText(), AiNodeEntryGetName(nentry), AiNodeGetName(node));
        return;
    }
    bool overrideConnection = false;
    SdfPath connection;

    if(hasConnection) {
#if PXR_VERSION > 2011
        // Find the attributes this input is getting its value from, which might
        // be an output or an input, including possibly itself if not connected
        const UsdShadeAttributeVector attrs = 
            UsdShadeUtils::GetValueProducingAttributes(input);

        if (!attrs.empty()) {
            if (attrs[0].HasAuthoredConnections() || 
                UsdShadeUtils::GetType(attrs[0].GetName()) == UsdShadeAttributeType::Input) {
                attr = attrs[0];
            } else {
                connection = attrs[0].GetPath();
                overrideConnection = true;
            }
        }
#else
        // Older USD versions, we check explicitely if we have a connection to a 
        // UsdShadeNodeGraph primitive, and consider this attribute instead
        SdfPathVector connections;
        attr.GetConnections(&connections);
        if (!connections.empty() && !connections[0].IsPrimPath()) {
            SdfPath primPath = connections[0].GetPrimPath();
            UsdPrim targetPrim = attr.GetPrim().GetStage()->GetPrimAtPath(primPath);
            if (targetPrim && targetPrim.IsA<UsdShadeNodeGraph>()) {
                std::string outputElement = connections[0].GetElementString();
                if (!outputElement.empty() && outputElement[0] == '.')
                    outputElement = outputElement.substr(1);
                
                UsdAttribute nodeGraphAttr = targetPrim.GetAttribute(TfToken(outputElement));
                if (nodeGraphAttr) {
                    attr = nodeGraphAttr;
                }
            }
        }
#endif
    }           

    int arrayType = AI_TYPE_NONE;
    if (paramType == AI_TYPE_ARRAY) {
        const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
        // Getting the default array, and checking its type
        arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;
    }
    InputUsdAttribute inputAttr(attr, overrideConnection ? &connection : nullptr);
    ReadAttribute(inputAttr, node, arnoldAttr, context.GetTimeSettings(), context, paramType, arrayType);
}

void UsdArnoldReadShader::ReadShaderInputs(const UsdPrim &prim, UsdArnoldReaderContext &context, 
    AtNode* node)
{
    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
    if (nodeEntry == nullptr) {
        return; // shouldn't happen
    }
    UsdShadeShader shader(prim);
    const TimeSettings& time = context.GetTimeSettings();

    // For OSL shaders, we first need to read the "code" attribute and set it, 
    // as it will change the AtNodeEntry
    bool isOsl = AiNodeIs(node, str::osl);
    if (isOsl) {
        UsdAttribute oslCode = prim.GetAttribute(str::t_inputs_code);
        VtValue value;
        if (oslCode && oslCode.Get(&value, time.frame)) {
            std::string code = VtValueGetString(value);
            if (!code.empty()) {
                AiNodeSetStr(node, str::code, AtString(code.c_str()));
                // Need to update the node entry that was
                // modified after "code" is set
                nodeEntry = AiNodeGetNodeEntry(node);
            }
        }
    }

    // Visit the inputs of this node to ensure they are emitted first.
    const std::vector<UsdShadeInput> shadeNodeInputs = shader.GetInputs();
    for (const UsdShadeInput& input : shadeNodeInputs) {

        TfToken inputName = input.GetBaseName();
        // osl "code" attribute was already handled previously,
        // we can skip it here
        if (isOsl && inputName == str::t_code)
            continue;

        if (inputName == str::t_name) {
            // If attribute "name" is set in the usd prim, we need to set the node name
            // accordingly. We also store this node original name in a map, that we
            // might use later on, when processing connections.
            VtValue nameValue;
            if (input.GetAttr().Get(&nameValue, time.frame)) {
                std::string nameStr = VtValueGetString(nameValue);
                std::string usdName = prim.GetPath().GetText();
                if ((!nameStr.empty()) && nameStr != usdName) {
                    AiNodeSetStr(node, str::name, AtString(nameStr.c_str()));
                    context.AddNodeName(usdName, node);
                }
            }
            continue;
        }
        _ReadShaderInput(input, node, inputName.GetString(), context);
    }
}
        
