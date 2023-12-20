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
#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <numeric>
#include <pxr/base/tf/token.h>
#include <constant_strings.h>
#include "materials_utils.h"
#include "api_adapter.h"
#include <pxr/base/gf/rotation.h>

#include <iostream>
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

using ShaderReadFunc = AtNode* (*)(const std::string& nodeName,  
    const std::vector<InputAttribute>& inputAttrs, ArnoldAPIAdapter& context, 
    const TimeSettings& time, MaterialReader& materialReader);

inline void _ReadShaderParameter(AtNode* node, const InputAttribute& attr, const std::string& arnoldAttr, 
    ArnoldAPIAdapter& context, const TimeSettings& time, 
    MaterialReader& materialReader, int paramType, int arrayType = AI_TYPE_NONE)
{
    if (!attr.connection.IsEmpty()) {
        materialReader.ConnectShader(node, arnoldAttr, attr.connection);
    } else {
        // if the attribute is connected, then there's no point in exporting its value
        // as it will be ignored
        ReadAttribute(attr, node, arnoldAttr, time, 
            context, paramType, arrayType);
    }
}


ShaderReadFunc ReadPreviewSurface = [](const std::string& nodeName,
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter& context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode*
{

    AtNode* node = materialReader.CreateArnoldNode("standard_surface", nodeName.c_str());
//std::cerr<<"Created node"<<AiNodeGetName(node)<<std::endl;
    AiNodeSetRGB(node, str::base_color, 0.18f, 0.18f, 0.18f);
    AiNodeSetFlt(node, str::base, 1.f); // scalar multiplier, set it to 1
    AiNodeSetRGB(node, str::emission_color, 0.f, 0.f, 0.f);
    AiNodeSetFlt(node, str::emission, 1.f); // scalar multiplier, set it to 1
    int useSpecularWorkflow = 0;
    AiNodeSetFlt(node, str::specular_roughness, 0.5);
    AiNodeSetFlt(node, str::specular_IOR, 1.5);
    AiNodeSetFlt(node, str::coat_roughness, 0.01f);

    for (const auto& attr : attrs) {
        if (attr.name == str::t_useSpecularWorkflow) { 
             useSpecularWorkflow = VtValueGetInt(attr.value);
             break;
         }
    }
     if (useSpecularWorkflow != 0) {
        AiNodeSetRGB(node, str::specular_color, 0.f, 0.f, 0.f);
    }
    for (const auto& attr : attrs) {
        if (attr.name == str::t_useSpecularWorkflow)
            continue;

        if (attr.name == str::t_diffuseColor) {
            _ReadShaderParameter(node, attr, "base_color", context, time, materialReader, AI_TYPE_RGB);
        } else if (attr.name == str::t_emissiveColor) {
            _ReadShaderParameter(node, attr, "emission_color", context, time, materialReader, AI_TYPE_RGB);
        } else if (attr.name == str::t_metallic && useSpecularWorkflow == 0) {
            _ReadShaderParameter(node, attr, "metalness", context, time, materialReader, AI_TYPE_FLOAT);
            // metallic workflow, set the specular color to white and use the
            // metalness
        } else if (attr.name == str::t_specularColor && useSpecularWorkflow != 0) {
            _ReadShaderParameter(node, attr, "specular_color", context, time, materialReader, AI_TYPE_RGB);
            // this is actually not correct. In USD, this is apparently the
            // fresnel 0° "front-facing" specular color. Specular is considered
            // to be always white for grazing angles
        } else if (attr.name == str::t_roughness) {
            _ReadShaderParameter(node, attr, "specular_roughness", context, time, materialReader, AI_TYPE_FLOAT);
        } else if (attr.name == str::t_ior) {
            _ReadShaderParameter(node, attr, "specular_IOR", context, time, materialReader, AI_TYPE_FLOAT);
        } else if (attr.name == str::t_clearcoat) {
            _ReadShaderParameter(node, attr, "coat", context, time, materialReader, AI_TYPE_FLOAT);
        } else if (attr.name == str::t_clearcoatRoughness) {
            _ReadShaderParameter(node, attr, "coat_roughness", context, time, materialReader, AI_TYPE_FLOAT);
        } else if (attr.name == str::t_opacity) {
            // Opacity is a bit complicated as it's a scalar value in USD, but a color in Arnold.
            // So we need to set it properly (see #998)
            // Arnold RGB           opacity       1, 1, 1
            // Arnold FLOAT         transmission  0
            // USD    FLOAT         opacity
            //AiNodeSetRGB(node, str::opacity, 1.f, 1.f, 1.f);

            // if the opacity attribute is linked, we can go through the usual read function
            const std::string subtractNodeName = nodeName + "@subtract";
            AtNode *subtractNode = materialReader.CreateArnoldNode("subtract", subtractNodeName.c_str());
            AiNodeSetRGB(subtractNode, str::input1, 1.f, 1.f, 1.f);
            float opacity;
            if (!attr.connection.IsEmpty()) {
                materialReader.ConnectShader(subtractNode, "input2", attr.connection);
            } else {
                float opacity = VtValueGetFloat(attr.value);
                // convert the input float value as RGB in the arnold shader
                AiNodeSetRGB(subtractNode, str::input2, opacity, opacity, opacity);
            }
            AiNodeLink(subtractNode, "transmission", node);    
        } else if (attr.name == str::t_normal) {
            if (!attr.connection.IsEmpty()) {
                // Usd expects a tangent normal map, let's create a normal_map
                // shader, and connect it there
                std::string normalMapName = nodeName + "@normal_map";
                AtNode *normalMap = materialReader.CreateArnoldNode("normal_map", normalMapName.c_str());
                AiNodeSetBool(normalMap, str::color_to_signed, false);
                materialReader.ConnectShader(normalMap, "input", attr.connection);
                AiNodeLink(normalMap, "normal", node);
            }
        }
    }
    // We're not exporting displacement (float) as it's part of meshes in
    // arnold. We're also not exporting the occlusion parameter (float),
    // since it doesn't really apply for arnold.
    return node;
};

ShaderReadFunc ReadUVTexture = [](const std::string& nodeName,
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    AtNode* node = materialReader.CreateArnoldNode("image", nodeName.c_str());
    //std::cerr<<"created "<<AiNodeGetName(node)<<std::endl;
    AiNodeSetStr(node, str::swrap, str::file);
    AiNodeSetStr(node, str::twrap, str::file);
    // To be consistent with USD, we ignore the missing textures
    AiNodeSetBool(node, str::ignore_missing_textures, true);

    auto ConvertWrap = [](const VtValue& value, AtNode *node, 
            const AtString &arnoldName) 
    { 
        std::string wrap = VtValueGetString(value);
        if (wrap == "repeat") 
            AiNodeSetStr(node, arnoldName, str::periodic);
        else if (wrap == "mirror")
            AiNodeSetStr(node, arnoldName, str::mirror);
        else if (wrap == "clamp")
            AiNodeSetStr(node, arnoldName, str::clamp);
        else if (wrap == "black")
            AiNodeSetStr(node, arnoldName, str::black);
    };
        
    for (const auto& attr : attrs) {
        if (attr.name == str::t_file) {
            _ReadShaderParameter(node, attr, "filename", context, time, materialReader, AI_TYPE_STRING);
        } else if (attr.name == str::t_st) {
            // first convert the "st" parameter, which will likely 
            // connect the shader plugged in the st attribute
            std::string varName;
            if (!attr.connection.IsEmpty()) {
                VtValue connectedVarName;
                TfToken connectedShaderId;
                if (materialReader.GetShaderInput(attr.connection, str::t_varname, connectedVarName, connectedShaderId) &&
                        TfStringStartsWith(connectedShaderId.GetString(), str::t_UsdPrimvarReader_.GetString())) {
                    varName = VtValueGetString(connectedVarName);
                }
            }
            if (varName.empty()) {
                // we haven't been able to identify which uvset needs to be used for our image shader,
                // so we translate the whole shading tree as usual. Note that shading trees returning
                // a uv coordiate to image.uvcoords is not preferred as derivatives can't be provided
                // and therefore texture filtering / efficient mipmapping is lost.
                _ReadShaderParameter(node, attr, "uvcoords", context, time, materialReader, AI_TYPE_VECTOR2);
            }
            else if (varName != "st" && varName != "uv") {
                // by default, the image shader will look for builtin UVs, unless we specify it
                // in the "uvset" parameter
                AiNodeSetStr(node, str::uvset, AtString(varName.c_str()));
            }
        } else if (attr.name == str::t_fallback) {
            _ReadShaderParameter(node, attr, "missing_texture_color", context, time, materialReader, AI_TYPE_RGBA);
        } else if (attr.name == str::t_scale) {
            GfVec4f v = VtValueGetVec4f(attr.value);
            AiNodeSetRGB(node, str::multiply, v[0], v[1], v[2]);
        } else if (attr.name == str::t_bias) {
            GfVec4f v = VtValueGetVec4f(attr.value);
            AiNodeSetRGB(node, str::offset, v[0], v[1], v[2]);
        } else if (attr.name == str::t_wrapS) {
            ConvertWrap(attr.value, node, str::swrap);
        } else if (attr.name == str::t_wrapT) {
            ConvertWrap(attr.value, node, str::twrap);
        } else if (attr.name == str::t_sourceColorSpace) {
            _ReadShaderParameter(node, attr, "color_space", context, time, materialReader, AI_TYPE_STRING);
        }
    }
    return node;
};

ShaderReadFunc ReadPrimvarFloat = [](const std::string& nodeName,
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    AtNode* node = materialReader.CreateArnoldNode("user_data_float", nodeName.c_str());
    for (const auto& attr : attrs) {
        if (attr.name == str::t_varname) {
            _ReadShaderParameter(node, attr, "attribute", context, time, materialReader, AI_TYPE_STRING);
        } else if (attr.name == str::t_fallback) {
            _ReadShaderParameter(node, attr, "default", context, time, materialReader, AI_TYPE_FLOAT);
        }
    }
    return node;
};

ShaderReadFunc ReadPrimvarFloat2 = [](const std::string& nodeName,
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {
    
    // If the user data attribute name is "st" or "uv", this actually
    // means that we should be looking at the builtin uv coordinates. 
 
    std::string varName;
    GfVec2f fallback(0.f);
    AtNode* node = nullptr;
    for (const auto& attr : attrs) {
        if (attr.name == str::t_varname) {
            varName = VtValueGetString(attr.value);
        } else if (attr.name == str::t_fallback) {
            fallback = VtValueGetVec2f(attr.value);
        }
    }
    if (varName == "st" || varName == "uv") {
        // For "st" and "uv" the user_data shader won't help and instead we want to 
        // create a utility shader returning the uvs
        node = materialReader.CreateArnoldNode("utility", nodeName.c_str());
        AiNodeSetStr(node, str::shade_mode, str::flat);
        AiNodeSetStr(node, str::color_mode, str::uv);
    } else {
        // Create a user_data shader that will lookup the user data (primvar)
        // and return its value
        node = materialReader.CreateArnoldNode("user_data_rgb", nodeName.c_str());
        AiNodeSetStr(node, str::attribute, AtString(varName.c_str()));
        AiNodeSetRGB(node, str::_default, fallback[0], fallback[1], 0.f);
    }
    return node;
};

ShaderReadFunc ReadPrimvarFloat3 = [](const std::string& nodeName,
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {
    
    AtNode* node = materialReader.CreateArnoldNode("user_data_rgb", nodeName.c_str());
    for (const auto& attr : attrs) {
        if (attr.name == str::t_varname) {
            _ReadShaderParameter(node, attr, "attribute", context, time, materialReader, AI_TYPE_STRING);
        } else if (attr.name == str::t_fallback) {
            _ReadShaderParameter(node, attr, "default", context, time, materialReader, AI_TYPE_RGB);
        }
    }
    return node;
};
ShaderReadFunc ReadPrimvarFloat4 = [](const std::string& nodeName,
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    AtNode* node = materialReader.CreateArnoldNode("user_data_rgba", nodeName.c_str());
    for (const auto& attr : attrs) {
        if (attr.name == str::t_varname) {
            _ReadShaderParameter(node, attr, "attribute", context, time, materialReader, AI_TYPE_STRING);
        } else if (attr.name == str::t_fallback) {
            _ReadShaderParameter(node, attr, "default", context, time, materialReader, AI_TYPE_RGBA);
        }
    }
    return node;
};

ShaderReadFunc ReadPrimvarInt = [](const std::string& nodeName,
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {
    
    AtNode* node = materialReader.CreateArnoldNode("user_data_int", nodeName.c_str());
    for (const auto& attr : attrs) {
        if (attr.name == str::t_varname) {
            _ReadShaderParameter(node, attr, "attribute", context, time, materialReader, AI_TYPE_STRING);
        } else if (attr.name == str::t_fallback) {
            _ReadShaderParameter(node, attr, "default", context, time, materialReader, AI_TYPE_INT);
        }
    }
    return node;
};

ShaderReadFunc ReadPrimvarString = [](const std::string& nodeName,
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    AtNode* node = materialReader.CreateArnoldNode("user_data_string", nodeName.c_str());
    for (const auto& attr : attrs) {
    if (attr.name == str::t_varname) {
            _ReadShaderParameter(node, attr, "attribute", context, time, materialReader, AI_TYPE_STRING);
        } else if (attr.name == str::t_fallback) {
            _ReadShaderParameter(node, attr, "default", context, time, materialReader, AI_TYPE_STRING);
        }
    }
    return nullptr;  
};

ShaderReadFunc ReadTransform2 = [](const std::string& nodeName,
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    AtNode* node = materialReader.CreateArnoldNode("matrix_multiply_vector", nodeName.c_str());
    GfVec2f translation = GfVec2f(0.f, 0.f);
    GfVec2f scale = GfVec2f(1.f, 1.f);
    float rotation = 0.f;

    for (const auto& attr : attrs) {


        if (attr.name == str::t_in) {
            _ReadShaderParameter(node, attr, "input", context, time, materialReader, AI_TYPE_RGB);
        } else if (attr.name == str::t_translation) {
            translation = VtValueGetVec2f(attr.value);
        } else if (attr.name == str::t_scale) {
            scale = VtValueGetVec2f(attr.value);
        } else if (attr.name == str::t_rotation) {
            rotation = VtValueGetFloat(attr.value);
        }
    }
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
        
    return node;  
};

using ShaderReadFuncs = std::unordered_map<TfToken, ShaderReadFunc, TfToken::HashFunctor>;
const ShaderReadFuncs& _ShaderReadFuncs()
{
    static const ShaderReadFuncs shaderReadFuncs{
        {str::t_UsdPreviewSurface, ReadPreviewSurface},      {str::t_UsdUVTexture, ReadUVTexture},
        {str::t_UsdPrimvarReader_float, ReadPrimvarFloat},   {str::t_UsdPrimvarReader_float2, ReadPrimvarFloat2},
        {str::t_UsdPrimvarReader_float3, ReadPrimvarFloat3}, {str::t_UsdPrimvarReader_point, ReadPrimvarFloat3},
        {str::t_UsdPrimvarReader_normal, ReadPrimvarFloat3}, {str::t_UsdPrimvarReader_vector, ReadPrimvarFloat3},
        {str::t_UsdPrimvarReader_float4, ReadPrimvarFloat4}, {str::t_UsdPrimvarReader_int, ReadPrimvarInt},
        {str::t_UsdPrimvarReader_string, ReadPrimvarString}, {str::t_UsdTransform2d, ReadTransform2},
    };
    return shaderReadFuncs;
}


AtNode* ReadArnoldShader(const std::string& nodeName, const TfToken& shaderId, 
    const std::vector<InputAttribute>& attrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader)
{
    //std::cerr<<"Read Arnold shader "<<nodeName<<" "<<shaderId.GetText()<<std::endl;
    AtNode* node = materialReader.CreateArnoldNode(shaderId.GetText(), nodeName.c_str());
    if (node == nullptr)
        return nullptr;

    //std::cerr<<"created "<<AiNodeGetName(node)<<std::endl;

    const AtNodeEntry *nentry = AiNodeGetNodeEntry(node);

    bool isOsl = (shaderId == str::t_osl);
    // For OSL shaders, we first want to read the "code" attribute, as it will
    // change the nodeEntry.
    if (isOsl) {
        //std::cerr<<"it's OSL"<<std::endl;
        for (const auto& attr : attrs) {
            if (attr.name == str::t_code) { 
                std::string code = VtValueGetString(attr.value);
                if (!code.empty()) {
                    //std::cerr<<"setting code "<<std::endl;
                    AiNodeSetStr(node, str::code, AtString(code.c_str()));
                    // Need to update the node entry that was
                    // modified after "code" is set
                    nentry = AiNodeGetNodeEntry(node);
                }
                break;
            }
        }
        // read code first
    }
    for (const auto& attr : attrs) {
        if (attr.name == str::t_name) {
            // If attribute "name" is set in the usd prim, we need to set the node name
            // accordingly. We also store this node original name in a map, that we
            // might use later on, when processing connections.
            VtValue nameValue;
            if (!attr.value.IsEmpty()) {
                std::string nameStr = VtValueGetString(attr.value);
                if ((!nameStr.empty()) && nameStr != nodeName) {
                    AiNodeSetStr(node, str::name, AtString(nameStr.c_str()));
                    context.AddNodeName(nodeName, node);
                }
            }
            continue;
        }
        if (isOsl && attr.name == str::t_code) 
            continue; // code was already translated

        const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nentry, AtString(attr.name.GetText()));
        if (paramEntry == nullptr) {

            // For links on array elements, we define a custom attribute type,
            // e.g. for array attribute "ramp_colors", we can link element 2
            // as "ramp_colors:i2"
            size_t elemPos = attr.name.GetString().find(":i");
            if (elemPos != std::string::npos) {
                // Read link to an array element
                std::string baseAttrName = attr.name.GetString();//.substr(0, elemPos);
                //std::cerr<<"from basename "<<baseAttrName<<std::endl;
                baseAttrName.replace(elemPos, 2, std::string("["));
                baseAttrName += "]";
                //std::cerr<<"got a basename "<<baseAttrName<<std::endl;
                materialReader.ConnectShader(node, baseAttrName, attr.connection);
                continue;
            }
            AiMsgWarning(
                "Arnold attribute %s not recognized in %s for %s", 
                attr.name.GetText(), AiNodeEntryGetName(nentry), AiNodeGetName(node));
            continue;
        }

        int paramType = AiParamGetType(paramEntry);
        int arrayType = AI_TYPE_NONE;
        if (paramType == AI_TYPE_ARRAY) {
            const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
            // Getting the default array, and checking its type
            arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;
        }

        if (!attr.connection.IsEmpty()) {
            //std::cerr<<"connect shgader "<<attr.connection<<" to " <<attr.name.GetString()<<std::endl;
            materialReader.ConnectShader(node, attr.name.GetString(), attr.connection);
            //std::cerr<<"connected "<<std::endl;
        } else {
            // if the attribute is connected, then there's no point in exporting its value
            // as it will be ignored
            ReadAttribute(attr, node, attr.name.GetString(), time, 
                context, paramType, arrayType);
            //std::cerr<<"done reading attr"<<std::endl;
        }
    }

    //std::cerr<<"finished for "<<nodeName<<" "<<std::endl;

    return node;
}

AtNode* ReadMtlxOslShader(const std::string& nodeName, 
    const std::vector<InputAttribute>& attrs, const TfToken& shaderId, 
    ArnoldAPIAdapter &context, const TimeSettings& time, 
    MaterialReader& materialReader, AtParamValueMap* params)
{
    // There is an OSL description for this materialx shader. 
    // Its attributes will be prefixed with "param_shader_"
    AtString oslCode;

    // The "params" argument was added to AiMaterialxGetOslShader in 7.2.0.0
#if ARNOLD_VERSION_NUM > 70104
    for (const auto &attr : attrs) {
        if(!attr.connection.IsEmpty()) {
            // Only the key is used, so we set an empty string for the value
            AiParamValueMapSetStr(params, AtString(attr.name.GetText()), AtString(""));
        }
    }
    oslCode = AiMaterialxGetOslShaderCode(shaderId.GetText(), "shader", params);
#elif ARNOLD_VERSION_NUM >= 70104
    oslCode = AiMaterialxGetOslShaderCode(shaderId.GetText(), "shader");
#endif

    if (!oslCode.empty()) {
        // Create an OSL inline shader
        AtNode* node = materialReader.CreateArnoldNode("osl", nodeName.c_str());       
        // Set the OSL code. This will create a new AtNodeEntry with parameters
        // based on the osl code
        AiNodeSetStr(node, str::code, oslCode);

        // Get the new node entry, after having set the code
        const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
        AiNodeDeclare(node, str::node_def, str::constantString);
        AiNodeSetStr(node, str::node_def, AtString(shaderId.GetText()));

        // Loop over the USD attributes of the shader
        for (const auto &attr : attrs) {
 
            // In order to match the usd attributes with the arnold node attributes, 
            // we need to add the prefix "param_shader_"
            std::string attrName = "param_shader_" + attr.name.GetString();
            AtString paramName(attrName.c_str()); 
            const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nodeEntry, paramName);

            if (paramEntry == nullptr) {
                // If we failed to find the attribute, try without the shader prefix
                // this is needed for non editable (BSDF/EDF/VDF) MaterialX node inputs
                attrName = "param_" + attr.name.GetString();
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
                std::string filename = VtValueGetString(attr.value);
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
                        std::string sourceCode = nodeName + std::string("_texturesource_") + attr.name.GetString();
                        // Create an additional osl shader, for the texture resource. Set it the
                        // hardcoded osl code above
                        AtNode *oslSource = materialReader.CreateArnoldNode("osl", sourceCode.c_str());
                        AiNodeSetStr(oslSource, str::code, tx_code);
                        // Set the actual texture filename to this new osl shader
                        AiNodeSetStr(oslSource, str::param_filename, AtString(filename.c_str()));

                        // Check if this "file" attribute has a colorSpace metadata 
                        /* FIXME : how to read metadatas ??!!! we should add an attribute
                           for colorSpace : attrName:colorSpace = string

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
                        */
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
            // Read the attribute value, as we do for regular attributes
            ReadAttribute(attr, node, attrName, time, context, paramType, arrayType);
        }
        return node;
    }
    return nullptr;
}
AtNode* ReadShader(const std::string& nodeName, const TfToken& shaderId, 
    const std::vector<InputAttribute>& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader)
{
    //std::cerr<<"Read Shader "<<nodeName<<" "<<shaderId<<std::endl;
    if (shaderId.IsEmpty())
        return nullptr;

    // First, we check if the shaderId starts with "arnold:", in which case
    // we're expecting to read an arnold native shader with a 1:1 mapping 
    if (TfStringStartsWith(shaderId.GetString(), str::t_arnold_prefix.GetString())) {
        TfToken arnoldShaderType(shaderId.GetString().substr(7).c_str());
        return ReadArnoldShader(nodeName, arnoldShaderType, inputAttrs, context, time, materialReader);
    } 

    // Check if there is a specific conversion function defined for this shader.
    // This is used for usd builtin shaders, like UsdPreviewSurface, UsdUvTexture, etc...
    const auto readIt = _ShaderReadFuncs().find(shaderId);
    if (readIt != _ShaderReadFuncs().end()) {
        return readIt->second(nodeName, inputAttrs, context, time, materialReader);
    }
    // Finally, we ask Arnold if this shader corresponds to a
    // materialx node definition

    // if a custom USD Materialx path is set, we need to provide it to 
    // Arnold's Materialx lib so that it can find custom node definitions
    AtParamValueMap *params = AiParamValueMap();
/*  FIXME
    const AtString &pxrMtlxPath = context.GetReader()->GetPxrMtlxPath();
    if (!pxrMtlxPath.empty()) {
        AiParamValueMapSetStr(params, str::MATERIALX_NODE_DEFINITIONS, pxrMtlxPath);
    }
*/
#if ARNOLD_VERSION_NUM > 70203
    const AtNodeEntry* shaderNodeEntry = AiMaterialxGetNodeEntryFromDefinition(shaderId.GetText(), params);
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
        AtString shaderNodeEntryName = AiNodeEntryGetNameAtString(shaderNodeEntry);
        if (shaderNodeEntryName == str::osl) {
            // This mtlx shader can be rendered by arnold as an OSL shader
            return ReadMtlxOslShader(nodeName, inputAttrs, shaderId, context, time, materialReader, params);
        } else {
            // This mtlx shader can be rendered by arnold as a native shader
            return ReadArnoldShader(nodeName, TfToken(shaderNodeEntryName.c_str()), inputAttrs, context, time, materialReader);
        }
    }
    AiParamValueMapDestroy(params);
    return nullptr;
}
