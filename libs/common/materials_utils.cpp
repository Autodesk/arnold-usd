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
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>
#include <numeric>
#include <pxr/base/tf/token.h>
#include <pxr/base/tf/stringUtils.h>
#include <constant_strings.h>
#include "materials_utils.h"
#include "api_adapter.h"
#include <pxr/base/gf/rotation.h>
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// Generic function pointer to translate a shader based on its shaderId
using ShaderReadFunc = AtNode* (*)(const std::string& nodeName,  
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter& context, 
    const TimeSettings& time, MaterialReader& materialReader);

/// Read an input attribute from the map, and set it in the required AtNode in a given arnoldAttr name 
template <typename T>
inline void _ReadShaderParameter(AtNode* node, const InputAttributesList& inputAttrs, const TfToken& attrName, const std::string& arnoldAttr, 
    ArnoldAPIAdapter& context, const TimeSettings& time, 
    MaterialReader& materialReader, int paramType, T defaultValue)
{
    // Check if an attribute of the expected input name can be found in the inputAttrs map
    const auto attrIt = inputAttrs.find(attrName);
    if (attrIt == inputAttrs.end()) {
        // The attribute isn't set in the list, we need to use the default value
        InputAttribute defaultAttr;
        defaultAttr.value = VtValue::Take(defaultValue);
        ReadAttribute(defaultAttr, node, arnoldAttr, time, context, paramType);
        return;
    }    
    const InputAttribute& attr = attrIt->second;

    if (!attr.connection.IsEmpty()) {
        // This attribute is linked, ask the MaterialReader to handle the connection.
        // In this case, we don't need to convert any VtValue as it will be ignored
        materialReader.ConnectShader(node, arnoldAttr, attr.connection, ArnoldAPIAdapter::CONNECTION_LINK);
    } else {
        ReadAttribute(attr, node, arnoldAttr, time, 
            context, paramType);
    }
}

/// Read a UsdPreviewSurface shader
ShaderReadFunc ReadPreviewSurface = [](const std::string& nodeName,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter& context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode*
{
    // UsdPreviewSurface is converted to an Arnold standard_surface
    AtNode* node = materialReader.CreateArnoldNode("standard_surface", nodeName.c_str());

    // First let's hardcode to 1 a couple of scalar multipliers 
    AiNodeSetFlt(node, str::base, 1.f);
    AiNodeSetFlt(node, str::emission, 1.f);
        
    _ReadShaderParameter(node, inputAttrs, str::t_diffuseColor, "base_color", context, time, 
            materialReader, AI_TYPE_RGB, GfVec3f(0.18f));

    _ReadShaderParameter(node, inputAttrs, str::t_emissiveColor, "emission_color", context, time, 
            materialReader, AI_TYPE_RGB, GfVec3f(0.f));

    // Specular Workflow : UsdPreviewSurface has 2 different ways of handling speculars, either
    // through a specular color, or with metalness. This is controlled by a toggle "useSpecularWorkflow"    
    int useSpecularWorkflow = 0;
    const auto useSpecularAttr = inputAttrs.find(str::t_useSpecularWorkflow);
    if (useSpecularAttr != inputAttrs.end() && !useSpecularAttr->second.value.IsEmpty())
        useSpecularWorkflow = VtValueGetInt(useSpecularAttr->second.value);
    
    if (useSpecularWorkflow != 0) {
        // Specular Workflow, we just read the specular color and leave the metalness to 0 
        _ReadShaderParameter(node, inputAttrs, str::t_specularColor, "specular_color", context, time, 
            materialReader, AI_TYPE_RGB, GfVec3f(0.f));
        // Note that this is actually not correct. In USD, this is apparently the
        // fresnel 0° "front-facing" specular color. Specular is considered
        // to be always white for grazing angles
    } else {
        // Metallic workflow, set the specular color to white and use the metalness
        AiNodeSetRGB(node, str::specular_color, 1.f, 1.f, 1.f);
        _ReadShaderParameter(node, inputAttrs, str::t_metallic, "metalness", context, time, materialReader, AI_TYPE_FLOAT, 0.f);
    }
    // Read a few input attributes, the 3rd argument being the input name and the 4th argument 
    // the corresponding Arnold attribute. We provide an default value based on the PreviewSurface 
    // specification, in case the attribute isn't found in the input list
    _ReadShaderParameter(node, inputAttrs, str::t_roughness, "specular_roughness", context, time, 
            materialReader, AI_TYPE_FLOAT, 0.5f);
    _ReadShaderParameter(node, inputAttrs, str::t_ior, "specular_IOR", context, time, 
            materialReader, AI_TYPE_FLOAT, 1.5f);
    _ReadShaderParameter(node, inputAttrs, str::t_clearcoat, "coat", context, time, 
            materialReader, AI_TYPE_FLOAT, 0.f);
    _ReadShaderParameter(node, inputAttrs, str::t_clearcoatRoughness, "coat_roughness", context, time, 
            materialReader, AI_TYPE_FLOAT, 0.01f);
    
    // Special case for opacity, we actually need to compute the complement (1-x) of the input
    // scalar opacity, and set it as transmission in the arnold standard_surface. This can be a 
    // bit tricky when this attribute is connected so we insert a shader to handle the complement
    const auto opacityAttr = inputAttrs.find(str::t_opacity);
    if (opacityAttr != inputAttrs.end()) {
        const std::string subtractNodeName = nodeName + "@subtract";
        // Create a subtract shader that will be connected to the transmission
        AtNode *subtractNode = materialReader.CreateArnoldNode("subtract", subtractNodeName.c_str());
        AiNodeSetRGB(subtractNode, str::input1, 1.f, 1.f, 1.f);
        const InputAttribute& attr = opacityAttr->second;
        if (!attr.connection.IsEmpty()) {
            materialReader.ConnectShader(subtractNode, "input2", attr.connection, ArnoldAPIAdapter::CONNECTION_LINK);
        } else {
            float opacity = VtValueGetFloat(attr.value);
            // convert the input float value as RGB in the arnold shader
            AiNodeSetRGB(subtractNode, str::input2, opacity, opacity, opacity);
        }
        AiNodeLink(subtractNode, "transmission", node);
    }
    
    const auto normalAttr = inputAttrs.find(str::t_normal);
    if (normalAttr != inputAttrs.end() && 
            !normalAttr->second.connection.IsEmpty()) {
        // Usd expects a tangent normal map, let's create a normal_map
        // shader, and connect it there
        std::string normalMapName = nodeName + "@normal_map";
        AtNode *normalMap = materialReader.CreateArnoldNode("normal_map", normalMapName.c_str());
        AiNodeSetBool(normalMap, str::color_to_signed, false);
        materialReader.ConnectShader(normalMap, "input", normalAttr->second.connection, ArnoldAPIAdapter::CONNECTION_LINK);
        AiNodeLink(normalMap, "normal", node);
    }

    // We're not exporting displacement (float) as it's part of meshes in
    // arnold. We're also not exporting the occlusion parameter (float),
    // since it doesn't really apply for arnold.
    return node;
};


/// Read a UsdUVTexture shader
ShaderReadFunc ReadUVTexture = [](const std::string& nodeName,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    // UsdUvTexture translates as an Arnold image node
    AtNode* node = materialReader.CreateArnoldNode("image", nodeName.c_str());

    _ReadShaderParameter(node, inputAttrs, str::t_file, "filename", context, 
        time, materialReader, AI_TYPE_STRING, std::string());
    _ReadShaderParameter(node, inputAttrs, str::t_fallback, "missing_texture_color", context, time, 
            materialReader, AI_TYPE_RGBA, GfVec4f(0.f, 0.f, 0.f, 1.f));
    _ReadShaderParameter(node, inputAttrs, str::t_sourceColorSpace, "color_space", context, time, 
        materialReader, AI_TYPE_STRING, std::string("auto"));
    // To be consistent with USD, we ignore the missing textures
    AiNodeSetBool(node, str::ignore_missing_textures, true);

    // Scale and Bias need to be converted from Vec4 to RGB
    auto ConvertVec4ToRGB = [](const InputAttributesList& inputAttrs, AtNode *node, 
                    const TfToken &usdName, const AtString &arnoldName)
    {
        const auto attr = inputAttrs.find(usdName);
        if (attr != inputAttrs.end()) {
            GfVec4f v = VtValueGetVec4f(attr->second.value);
            AiNodeSetRGB(node, arnoldName, v[0], v[1], v[2]);
        }
    };
    ConvertVec4ToRGB(inputAttrs, node, str::t_scale, str::multiply);
    ConvertVec4ToRGB(inputAttrs, node, str::t_bias, str::offset);

    // WrapS and WrapT strings need to be converted to the equivalent Arnold values
    auto ConvertWrap = [](const InputAttributesList& inputAttrs, AtNode *node, 
            const TfToken& usdName, const AtString &arnoldName) 
    { 
        const auto attr = inputAttrs.find(usdName);
        if (attr != inputAttrs.end()) {
            std::string wrap = VtValueGetString(attr->second.value);
            if (wrap == "repeat") 
                AiNodeSetStr(node, arnoldName, str::periodic);
            else if (wrap == "mirror")
                AiNodeSetStr(node, arnoldName, str::mirror);
            else if (wrap == "clamp")
                AiNodeSetStr(node, arnoldName, str::clamp);
            else if (wrap == "black")
                AiNodeSetStr(node, arnoldName, str::black);
            else // default is "use metadata"
                AiNodeSetStr(node, arnoldName, str::file);
        } else {
            // default is "use metadata"
            AiNodeSetStr(node, arnoldName, str::file);
        }
    };
    ConvertWrap(inputAttrs, node, str::t_wrapS, str::swrap);
    ConvertWrap(inputAttrs, node, str::t_wrapT, str::twrap);

    // st is the most complicated attribute to convert to Arnold. In UsdUvTexture, it's connected to a shading 
    // tree that returns the uv coordinates to use. This should be avoided as much as possible in Arnold, 
    // since such setups loose the texture derivatives and filtering. Here we try to identify the most 
    // common use cases and set the image shader in a way that is optimized for Arnold.
    const auto stAttr = inputAttrs.find(str::t_st);
    if (stAttr !=  inputAttrs.end()) {
        // first convert the "st" parameter, which will likely 
        // connect the shader plugged in the st attribute
        const InputAttribute& attr = stAttr->second;
        std::string varName;
        if (!attr.connection.IsEmpty()) {
            VtValue connectedVarName;
            TfToken connectedShaderId;
            // The st attribute is connected, let's ask the materialReader to look for the connected shader
            // and check its shaderId as well as its attribute "varname". Here we only consider use cases
            // where "st" is directly connected to a primvar reader shader.
            if (materialReader.GetShaderInput(attr.connection.GetPrimPath(), 
                    str::t_varname, connectedVarName, connectedShaderId) &&
                    TfStringStartsWith(connectedShaderId.GetString(), str::t_UsdPrimvarReader_.GetString())) {
                // varName tells us which primvar needs to be used for this uv texture
                varName = VtValueGetString(connectedVarName);
            }
        }
        if (varName == "st" || varName == "uv") {
            // default use case, we don't need to set any value in the uvset attribute
            // and Arnold will look for the builtin UVs
            AiNodeResetParameter(node, str::uvset);
        } else if (!varName.empty()) {
            // We need to specify a custom uv set in the image node
            AiNodeSetStr(node, str::uvset, AtString(varName.c_str()));
        } else {
            // we haven't been able to identify which uvset needs to be used for our image shader,
            // so we translate the whole shading tree as usual. Note that shading trees returning
            // a uv coordiate to image.uvcoords is not preferred as derivatives can't be provided
            // and therefore texture filtering / efficient mipmapping is lost.
            _ReadShaderParameter(node, inputAttrs, str::t_st, "uvcoords", context,
                time, materialReader, AI_TYPE_VECTOR2, GfVec2f(0.f));
        }
    }
    return node;
};

/// Translator for UsdPrimvarReader_float
ShaderReadFunc ReadPrimvarFloat = [](const std::string& nodeName,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    // Create an Arnold user_data_float shader
    AtNode* node = materialReader.CreateArnoldNode("user_data_float", nodeName.c_str());
    _ReadShaderParameter(node, inputAttrs, str::t_varname, "attribute", context,
                time, materialReader, AI_TYPE_STRING, std::string());
    _ReadShaderParameter(node, inputAttrs, str::t_fallback, "default", context,
                time, materialReader, AI_TYPE_FLOAT, 0.f);
    return node;
};

/// Translator for UsdPrimvarReader_float2
ShaderReadFunc ReadPrimvarFloat2 = [](const std::string& nodeName,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {
    
    // If the user data attribute name is "st" or "uv", this actually
    // means that we should be looking at the builtin uv coordinates. 
 
    std::string varName;
    AtNode* node = nullptr;
    const auto varnameAttr = inputAttrs.find(str::t_varname);
    if (varnameAttr != inputAttrs.end()) {
        varName = VtValueGetString(varnameAttr->second.value);
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
        const auto fallbackAttr = inputAttrs.find(str::t_fallback);
        if (fallbackAttr != inputAttrs.end()) {
            GfVec2f fallback = VtValueGetVec2f(fallbackAttr->second.value);
            AiNodeSetRGB(node, str::_default, fallback[0], fallback[1], 0.f);
        }        
    }
    return node;
};


/// Translator for UsdPrimvarReader_float3, UsdPrimvarReader_point, 
/// UsdPrimvarReader_vector and UsdPrimvarReader_normal
ShaderReadFunc ReadPrimvarFloat3 = [](const std::string& nodeName,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {
    
    // Create an Arnold user_data_rgb shader
    AtNode* node = materialReader.CreateArnoldNode("user_data_rgb", nodeName.c_str());
    _ReadShaderParameter(node, inputAttrs, str::t_varname, "attribute", context,
                time, materialReader, AI_TYPE_STRING, std::string());
    _ReadShaderParameter(node, inputAttrs, str::t_fallback, "default", context,
                time, materialReader, AI_TYPE_RGB, GfVec3f(0.f));
    return node;
};

/// Translator for UsdPrimvarReader_float4
ShaderReadFunc ReadPrimvarFloat4 = [](const std::string& nodeName,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    // Create an Arnold user_data_rgba shader
    AtNode* node = materialReader.CreateArnoldNode("user_data_rgba", nodeName.c_str());
    _ReadShaderParameter(node, inputAttrs, str::t_varname, "attribute", context,
                time, materialReader, AI_TYPE_STRING, std::string());
    _ReadShaderParameter(node, inputAttrs, str::t_fallback, "default", context,
                time, materialReader, AI_TYPE_RGBA, GfVec4f(0.f, 0.f, 0.f, 1.f));
    return node;
};
/// Translator for UsdPrimvarReader_int
ShaderReadFunc ReadPrimvarInt = [](const std::string& nodeName,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {
    
    // Create an Arnold user_data_int shader
    AtNode* node = materialReader.CreateArnoldNode("user_data_int", nodeName.c_str());
    _ReadShaderParameter(node, inputAttrs, str::t_varname, "attribute", context,
                time, materialReader, AI_TYPE_STRING, std::string());
    _ReadShaderParameter(node, inputAttrs, str::t_fallback, "default", context,
                time, materialReader, AI_TYPE_INT, (int)0);
    return node;
};

/// Translator for UsdPrimvarReader_string
ShaderReadFunc ReadPrimvarString = [](const std::string& nodeName,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    // Create an Arnold user_data_string shader
    AtNode* node = materialReader.CreateArnoldNode("user_data_string", nodeName.c_str());
    _ReadShaderParameter(node, inputAttrs, str::t_varname, "attribute", context,
                time, materialReader, AI_TYPE_STRING, std::string());
    _ReadShaderParameter(node, inputAttrs, str::t_fallback, "default", context,
                time, materialReader, AI_TYPE_STRING, std::string());
    return node;
};

/// Translator for UsdTransform2d
ShaderReadFunc ReadTransform2 = [](const std::string& nodeName,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader) -> AtNode* {

    // We create an Arnold matrix_multiply_vector that will take an input vector
    // and apply a matrix on top of it. We'll combine the input scale, rotation and translation
    // into a matrix value
    AtNode* node = materialReader.CreateArnoldNode("matrix_multiply_vector", nodeName.c_str());
    GfVec2f translation = GfVec2f(0.f, 0.f);
    GfVec2f scale = GfVec2f(1.f, 1.f);
    float rotation = 0.f;

    _ReadShaderParameter(node, inputAttrs, str::t_in, "input", context, 
        time, materialReader, AI_TYPE_RGB, GfVec3f(0.f));

    for (const auto& attr : inputAttrs) {
        if (attr.first == str::t_translation) {
            translation = VtValueGetVec2f(attr.second.value);
        } else if (attr.first == str::t_scale) {
            scale = VtValueGetVec2f(attr.second.value);
        } else if (attr.first == str::t_rotation) {
            rotation = VtValueGetFloat(attr.second.value);
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

/// Read an Arnold builtin shader, with a 1-1 mapping
AtNode* ReadArnoldShader(const std::string& nodeName, const TfToken& shaderId, 
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context, 
    const TimeSettings& time, MaterialReader& materialReader)
{
    AtNode* node = materialReader.CreateArnoldNode(shaderId.GetText(), nodeName.c_str());
    if (node == nullptr)
        return nullptr;

    const AtNodeEntry *nentry = AiNodeGetNodeEntry(node);

    bool isOsl = (shaderId == str::t_osl);
    // For OSL shaders, we first want to read the "code" attribute, as it will
    // change the nodeEntry.
    if (isOsl) {
        const auto codeAttr = inputAttrs.find(str::t_code);
        if (codeAttr != inputAttrs.end()) {
            std::string code = VtValueGetString(codeAttr->second.value);
            if (!code.empty()) {
                AiNodeSetStr(node, str::code, AtString(code.c_str()));
                // Need to update the node entry that was
                // modified after "code" is set
                nentry = AiNodeGetNodeEntry(node);
            }
        }            
    }
    // Loop through the input attributes, and only set these ones.
    // As opposed to UsdPreviewSurface translator, we'll be doing a 1-1 conversion here,
    // so we don't need to care about default values. The attributes that are not part of our
    // list won't be set and will therefore be left to their Arnold default.
    for (const auto& attrIt : inputAttrs) {
        const TfToken &attrName = attrIt.first;
        const std::string attrNameStr = attrName.GetString(); // to avoid calling GetString multiple times
        const InputAttribute& attr = attrIt.second;
#if PXR_VERSION >= 2505
        // In USD 25.05 additional parameters are passed to describe the type name and color space of the actual parameter.
        // They are prefixed with typeName and colorSpace. Since we don't need them we just skip them
        if (TfStringStartsWith(attrNameStr, "typeName:") || TfStringStartsWith(attrNameStr, "colorSpace:")) {
            continue;
        }
#endif
        if (attrName == str::t_name) {
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
        if (isOsl && attrName == str::t_code) 
            continue; // code was already translated

        // Get the AtParamEntry for this attribute name
        const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nentry, AtString(attrNameStr.c_str()));
        if (paramEntry == nullptr) {
            // The parameter entry wasn't found for this attribute. Either we asked for an unkown parameter,
            // of we're trying to translate an array index. 

            // For links on array elements, we define a custom attribute type,
            // e.g. for array attribute "ramp_colors", we can link element 2
            // as "ramp_colors:i2"
            size_t elemPos = attrNameStr.find(":i");
            if (elemPos != std::string::npos) {
                // Read link to an array element
                std::string baseAttrName = attrNameStr;
                baseAttrName.replace(elemPos, 2, std::string("["));
                baseAttrName += "]";

                std::string arrayName = baseAttrName.substr(0, elemPos);
                const AtParamEntry *arrayEntry = AiNodeEntryLookUpParameter(nentry, AtString(arrayName.c_str()));
                ArnoldAPIAdapter::ConnectionType connectionType = ArnoldAPIAdapter::CONNECTION_LINK;
                if (arrayEntry) {
                    const AtParamValue *defaultValue = AiParamGetDefault(arrayEntry);
                    if (defaultValue && AiArrayGetType(defaultValue->ARRAY()) == AI_TYPE_NODE)
                        connectionType = ArnoldAPIAdapter::CONNECTION_PTR;
                }

                materialReader.ConnectShader(node, baseAttrName, attr.connection, connectionType);
                continue;
            }
            // Silently skip channel-component attributes authored by the
            // application (e.g. "r", "g", "b", "a", "x", "y", "z", "rgb",
            // "rgba", "xyz"). They are valid USD outputs but have no
            // corresponding Arnold parameter.
            static const std::unordered_set<TfToken, TfToken::HashFunctor> kChannelAttrs{
                TfToken("r"), TfToken("g"), TfToken("b"), TfToken("a"),
                TfToken("x"), TfToken("y"), TfToken("z"),
                TfToken("rgb"), TfToken("rgba"), TfToken("xyz")
            };
            if (!kChannelAttrs.count(attrName)) {
                AiMsgWarning(
                    "Arnold attribute %s not recognized in %s for %s",
                    attrName.GetText(), AiNodeEntryGetName(nentry), AiNodeGetName(node));
            }
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
            // The attribute is linked, let's ask the MaterialReader to process the connection.
            // We don't need to read the VtValue here, as arnold will ignore it
            materialReader.ConnectShader(node, attrNameStr, attr.connection, ArnoldAPIAdapter::CONNECTION_LINK);
        } else {
            ReadAttribute(attr, node, attrNameStr, time, 
                context, paramType, arrayType);
        }
    }

    return node;
}

/// How a MaterialX coordinate-space value resolves to Arnold.
///
/// Houdini/Solaris author coord-sys space names with a colon separator and
/// lower-case projection names, e.g. "map_proj", "map_proj:ndc",
/// "map_proj:raster", "map_proj:screen".
enum class CoordSysSpaceKind {
    Standard,   ///< Built-in OSL space (world/object/...) - used verbatim.
    Projective, ///< ".NDC"/".screen"/".raster" - resolved through the coordSys
                ///< *camera* node (needs a frustum). @p arnoldValue holds the
                ///< dotted "<name>.<SUFFIX>" the node-graph remap expects.
    Affine,     ///< plain "<name>" or "<name>.camera" - resolved through the
                ///< coordSys *matrix* (scale/shear preserved) via an inserted
                ///< matrix_multiply_vector helper. @p name holds the coordSys name.
};

/// Classify a coordinate-space input value. Sets @p name (the bare coordinate-
/// system name) for the Projective/Affine cases and @p arnoldValue (the value to
/// store on the OSL node) for the Standard/Projective cases.
static CoordSysSpaceKind _ClassifyCoordSysSpace(
    const std::string& value, std::string& name, std::string& arnoldValue)
{
    arnoldValue = value;
    if (value.empty())
        return CoordSysSpaceKind::Standard;
    static const std::unordered_set<std::string> standardSpaces = {
        "model", "object", "world", "common", "shader", "tangent"};
    const size_t sep = value.find_first_of(".:");
    if (sep == std::string::npos) {
        if (standardSpaces.count(value) != 0)
            return CoordSysSpaceKind::Standard;
        // A plain non-standard name is a coordinate-system reference; without a
        // suffix it means the coord-sys ("camera"-equivalent) space, i.e. affine.
        name = value;
        return CoordSysSpaceKind::Affine;
    }
    name = value.substr(0, sep);
    std::string suffix = value.substr(sep + 1);
    if (suffix == "ndc")
        suffix = "NDC";
    if (suffix == "NDC" || suffix == "screen" || suffix == "raster") {
        arnoldValue = name + "." + suffix;
        return CoordSysSpaceKind::Projective;
    }
    // "camera" (or anything else) resolves through the matrix, preserving scale.
    return CoordSysSpaceKind::Affine;
}

/// For a MaterialX node that can consume a USD coordinate system, the
/// matrix_multiply_vector "type" (point/vector/normal) matching its geometric
/// quantity; nullptr for any other node.
static const char* _MtlxCoordSysHelperType(const TfToken& shaderId)
{
    const std::string& s = shaderId.GetString();
    if (!TfStringStartsWith(s, "ND_"))
        return nullptr;
    if (TfStringStartsWith(s, "ND_position_") || TfStringStartsWith(s, "ND_transformpoint_"))
        return "point";
    if (TfStringStartsWith(s, "ND_normal_") || TfStringStartsWith(s, "ND_transformnormal_"))
        return "normal";
    if (TfStringStartsWith(s, "ND_tangent_") || TfStringStartsWith(s, "ND_bitangent_") ||
        TfStringStartsWith(s, "ND_transformvector_"))
        return "vector";
    return nullptr;
}

/// Read a MaterialX shader through OSL
AtNode* ReadMtlxOslShader(const std::string& nodeName,
    const InputAttributesList& inputAttrs, const TfToken& shaderId, 
    ArnoldAPIAdapter &context, const TimeSettings& time, 
    MaterialReader& materialReader, AtParamValueMap* params)
{
    // There is an OSL description for this materialx shader. 
    // Its attributes will be prefixed with "param_shader_"
    AtString oslCode;

    // The "params" argument was added to AiMaterialxGetOslShader in 7.2.0.0
#if ARNOLD_VERSION_NUM > 70104
    std::string shaderKey(shaderId.GetString());
    const AtString &pxrMtlxPath = context.GetPxrMtlxPath();
    if (!pxrMtlxPath.empty()) {
        shaderKey += pxrMtlxPath.c_str();
    }
    // inputAttrs is an unordered_map, so iteration order is unspecified.
    // Previously we appended connected attribute names to shaderKey in
    // iteration order, so two semantically-identical shader inputs could hash
    // to different cache keys, defeating the OSL code cache. Collect connected
    // names into a sorted vector first to make the key deterministic.
    std::vector<std::string> connectedAttrNames;
    connectedAttrNames.reserve(inputAttrs.size());
    for (const auto& attrIt : inputAttrs) {
        if (!attrIt.second.connection.IsEmpty()) {
            connectedAttrNames.emplace_back(attrIt.first.GetString());
        }
    }
    std::sort(connectedAttrNames.begin(), connectedAttrNames.end());
    for (const auto& name : connectedAttrNames) {
        // Only the key is used, so we set an empty string for the value
        AiParamValueMapSetStr(params, AtString(name.c_str()), AtString(""));
        shaderKey += name;
    }
    oslCode = context.GetCachedOslCode(shaderKey, shaderId.GetText(), params);
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

        // Affine coordinate-system sides collected while reading this node's
        // space/fromspace/tospace inputs; turned into matrix_multiply_vector
        // helpers after the node is fully built (see below), their "matrix" input
        // set by value from the coordSys's matrix (HdArnoldNodeGraph::RemapCoordSysSpaces).
        // The affine path needs the matrix_multiply_vector node type: when it is
        // missing (older Arnold), fall back to the camera-node string path instead
        // of inserting a helper whose "matrix" input would never be set (which
        // would collapse to identity).
        static const bool haveMatrixMultiplyVector = AiNodeEntryLookUp(AtString("matrix_multiply_vector")) != nullptr;
        const char* coordHelperType = haveMatrixMultiplyVector ? _MtlxCoordSysHelperType(shaderId) : nullptr;
        struct _AffineSide {
            bool inverse;     ///< world->local (space/tospace) vs local->world (fromspace).
            std::string name; ///< coordinate-system name.
        };
        std::vector<_AffineSide> affineSides;

        // Loop over the USD attributes of the shader
        for (const auto &attrIt : inputAttrs) {
            const TfToken& attrName = attrIt.first;
            if (attrName == str::t_code)
                continue;
            const InputAttribute& attr = attrIt.second;
 
            // In order to match the usd attributes with the arnold node attributes, 
            // we need to add the prefix "param_shader_"
            std::string attrNameStr = "param_shader_" + attrName.GetString();
            AtString paramName(attrNameStr.c_str()); 
            const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nodeEntry, paramName);

            if (paramEntry == nullptr) {
                // If we failed to find the attribute, try without the shader prefix
                // this is needed for non editable (BSDF/EDF/VDF) MaterialX node inputs
                attrNameStr = "param_" + attrName.GetString();
                paramName = AtString(attrNameStr.c_str());
                paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(attrNameStr.c_str()));
                if (paramEntry == nullptr) {
                    // Couldn't find this attribute in the osl entry
                    continue;
                }
            }
            uint8_t paramType = AiParamGetType(paramEntry);
            
#if ARNOLD_VERSION_NUM < 70400
            // The tiledimage / image shaders need to create
            // an additional osl shader to represent the filename
            if (paramType == AI_TYPE_POINTER && TfStringStartsWith(attrNameStr, "param_shader_file")) {
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
                        std::string sourceCode = nodeName + std::string("_texturesource_") + attrName.GetString();
                        // Create an additional osl shader, for the texture resource. Set it the
                        // hardcoded osl code above
                        AtNode *oslSource = materialReader.CreateArnoldNode("osl", sourceCode.c_str());
                        AiNodeSetStr(oslSource, str::code, tx_code);
                        // Set the actual texture filename to this new osl shader
                        AiNodeSetStr(oslSource, str::param_filename, AtString(filename.c_str()));

                        // Check if this "file" attribute has a colorSpace metadata, that we have 
                        // set as a separate parameter
                        std::string colorSpaceStr = std::string("colorSpace:")+ attrName.GetString();
                        TfToken colorSpace(colorSpaceStr);
                        const auto colorSpaceAttr = inputAttrs.find(colorSpace);
                        if (colorSpaceAttr != inputAttrs.end()) {
                            std::string colorSpaceStr = VtValueGetString(colorSpaceAttr->second.value);
                            AiNodeSetStr(oslSource, str::param_colorspace, AtString(colorSpaceStr.c_str()));

                        } else {
                            AiNodeSetStr(oslSource, str::param_colorspace, str::_auto);
                        }
                        // Connect the original osl shader attribute to our new osl shader
                        AiNodeLink(oslSource,paramName, node);
                        continue;
                    }
               }
           }
#else
            if (paramType == AI_TYPE_STRING && TfStringStartsWith(attrNameStr, "param_shader_file")) {
                std::string filename = VtValueGetString(attr.value);
                // if the filename is empty, there's nothing else to do
                if (!filename.empty()) {
                    // get the metadata "osl_struct" on the arnold attribute for "file", it should be set to "textureresource"
                    AtString fileStr;
                    // Check if this "file" attribute has a colorSpace metadata, that we have
                    // set as a separate parameter
                    std::string colorSpaceStr = std::string("colorSpace:")+ attrName.GetString();
                    TfToken colorSpace(colorSpaceStr);
                    const auto colorSpaceAttr = inputAttrs.find(colorSpace);
                    AtString colorspace_param((attrNameStr + "_colorspace").c_str());
                    if (colorSpaceAttr != inputAttrs.end()) {
                        std::string colorSpaceStr = VtValueGetString(colorSpaceAttr->second.value);
                        AiNodeSetStr(node, colorspace_param, AtString(colorSpaceStr.c_str()));
                    } else {
                        AiNodeSetStr(node, colorspace_param, str::_auto);
                    }
                }
            }
#endif
            int arrayType = AI_TYPE_NONE;
            if (paramType == AI_TYPE_ARRAY) {
                const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
                // Getting the default array, and checking its type
                arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;
            } else if (!attr.connection.IsEmpty()) {
                // This attribute is linked, ask the MaterialReader to handle the connection.
                // In this case, we don't need to convert any VtValue as it will be ignored
                materialReader.ConnectShader(node, attrNameStr, attr.connection, ArnoldAPIAdapter::CONNECTION_LINK);
                continue;
            }

            // A MaterialX geometric node (ND_position_vector3, ND_normal_vector3,
            // ...) exposes a "space" string input, and a transform node
            // (ND_transformpoint_vector3, ...) exposes "fromspace"/"tospace",
            // that may reference a USD coordinate system. Only when the value
            // really holds a string: VtValueGetString yields an empty string for
            // anything else, which would clobber the OSL node's default space.
            // Any other value type falls through to ReadAttribute.
            if (paramType == AI_TYPE_STRING &&
                (attrName == str::t_space || attrName == str::t_fromspace || attrName == str::t_tospace) &&
                (attr.value.IsHolding<std::string>() || attr.value.IsHolding<TfToken>())) {
                std::string csName, arnoldValue;
                const CoordSysSpaceKind kind =
                    _ClassifyCoordSysSpace(VtValueGetString(attr.value), csName, arnoldValue);
                if (kind == CoordSysSpaceKind::Affine && coordHelperType != nullptr) {
                    // Neutralise this side to "world" and defer the transform to a
                    // matrix_multiply_vector helper (created after this loop) whose
                    // matrix carries the coordinate system's full transform, scale
                    // and shear included - which the camera-node path strips. The
                    // "space"/"tospace" sides map world->local (inverse), while
                    // "fromspace" maps local->world (forward).
                    AiNodeSetStr(node, paramName, AtString("world"));
                    const bool inverse = (attrName == str::t_space || attrName == str::t_tospace);
                    affineSides.push_back({inverse, csName});
                } else {
                    // Standard built-in space, or a projective space resolved
                    // through the coordSys camera node by the node-graph remap.
                    AiNodeSetStr(node, paramName, AtString(arnoldValue.c_str()));
                }
                continue;
            }

            // Read the attribute value, as we do for regular attributes
            ReadAttribute(attr, node, attrNameStr, time,
                    context, paramType, arrayType);

        }

        // Turn any collected affine coordinate-system sides into a chain of
        // matrix_multiply_vector helpers appended after this node. Forward
        // (local->world) sides are applied before inverse (world->local) ones so
        // a transform node's fromspace/tospace compose as in * M_from * inv(M_to).
        // Each helper's "matrix" input is left unset here; the node-graph remap
        // sets it by value, per-rprim, from the coordinate system's matrix, which
        // preserves scale/shear (see HdArnoldNodeGraph::RemapCoordSysSpaces). The
        // helper name encodes "|csmtx|<index>|<f|i>|<coordSysName>" so the remap
        // can find it and pick the forward/inverse matrix. Downstream consumers of
        // this node are redirected to the last helper via AddNodeName.
        if (!affineSides.empty()) {
            std::stable_sort(affineSides.begin(), affineSides.end(),
                [](const _AffineSide& a, const _AffineSide& b) { return !a.inverse && b.inverse; });
            AtNode* src = node;
            int helperIndex = 0;
            for (const auto& side : affineSides) {
                const std::string helperName = nodeName + "|csmtx|" + std::to_string(helperIndex++) +
                    "|" + (side.inverse ? "i" : "f") + "|" + side.name;
                AtNode* helper = materialReader.CreateArnoldNode("matrix_multiply_vector", helperName.c_str());
                if (helper == nullptr)
                    break;
                AiNodeSetStr(helper, str::type, AtString(coordHelperType));
                AiNodeLink(src, "input", helper);
                src = helper;
            }
            if (src != node) {
                context.AddNodeName(nodeName, src);
                return src;
            }
        }
        return node;
    }
    return nullptr;
}

/// Read a shader with a given shaderId and translate it to Arnold.
AtNode* ReadShader(const std::string& nodeName, const TfToken& shaderId,
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter &context,
    const TimeSettings& time, MaterialReader& materialReader)
{
    if (shaderId.IsEmpty())
        return nullptr;

    // First, we check if the shaderId starts with "arnold:", in which case
    // we're expecting to read an arnold native shader with a 1:1 mapping
    if (TfStringStartsWith(shaderId.GetString(), str::t_arnold_prefix.GetString())) {
        TfToken arnoldShaderType(
            shaderId.GetString().substr(str::t_arnold_prefix.GetString().size()).c_str());
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

    const AtString &pxrMtlxPath = context.GetPxrMtlxPath();
    if (!pxrMtlxPath.empty()) {
        AiParamValueMapSetStr(params, str::MATERIALX_NODE_DEFINITIONS, pxrMtlxPath);
    }

#if ARNOLD_VERSION_NUM > 70203
    std::string shaderKey = shaderId.GetString();
    shaderKey += std::string(pxrMtlxPath.empty() ? "" : std::string(pxrMtlxPath.c_str()));
    const AtNodeEntry* shaderNodeEntry = context.GetCachedMtlxNodeEntry(shaderKey, shaderId.GetText(), params);
#else
    // arnold backwards compatibility. We used to rely on the nodedef prefix to identify 
    // the shader type
    AtString shaderEntryStr;
    const char* shaderIdStr = shaderId.GetText();
    if (shaderId == str::t_ND_standard_surface_surfaceshader)
        shaderEntryStr = str::standard_surface;
    else if (strncmp(shaderIdStr, "ND_", 3) == 0)
        shaderEntryStr = str::osl;
    else if (strncmp(shaderIdStr, "ARNOLD_ND_", 10) == 0)
        shaderEntryStr = AtString(shaderIdStr + 10);

    const AtNodeEntry *shaderNodeEntry = shaderEntryStr.empty() ? 
        nullptr : AiNodeEntryLookUp(shaderEntryStr);
#endif
    if (shaderNodeEntry) {
        AtString shaderNodeEntryName = AiNodeEntryGetNameAtString(shaderNodeEntry);
        AtNode* result = nullptr;
        if (shaderNodeEntryName == str::osl) {
            // This mtlx shader can be rendered by arnold as an OSL shader
            result = ReadMtlxOslShader(nodeName, inputAttrs, shaderId, context, time, materialReader, params);
        } else {
            // This mtlx shader can be rendered by arnold as a native shader
            result = ReadArnoldShader(nodeName, TfToken(shaderNodeEntryName.c_str()), inputAttrs, context, time, materialReader);
        }
        AiParamValueMapDestroy(params);
        return result;
    }
    AiParamValueMapDestroy(params);
    return nullptr;
}
