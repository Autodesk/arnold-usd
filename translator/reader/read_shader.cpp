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

#include "registry.h"
#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/** Export USD native shaders to Arnold
 *
 **/
void UsdArnoldReadShader::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    std::string nodeName = prim.GetPath().GetText();
    UsdShadeShader shader(prim);
    // The "Shader Id" will tell us what is the type of the shader
    TfToken id;
    shader.GetIdAttr().Get(&id);
    std::string shaderId = id.GetString();
    AtNode *node = NULL;

    // Support shaders having info:id = ArnoldStandardSurface
    if (shaderId.length() > 6 && shaderId[0] == 'A' && shaderId[1] == 'r' && shaderId[2] == 'n' && shaderId[3] == 'o' &&
        shaderId[4] == 'l' && shaderId[5] == 'd') {
        // We have a USD shader which shaderId is an arnold node name. The
        // result should be equivalent to a custom USD node type with the same
        // name. Let's search in the registry if there is a reader for that type
        UsdArnoldPrimReader *primReader = context.getReader()->getRegistry()->getPrimReader(shaderId);
        if (primReader) {
            primReader->read(prim, context); // read this primitive
        }
        return;
    }
    // Support shaders having info:id = arnold:standard_surface
    if (shaderId.length() > 7 && shaderId[0] == 'a' && shaderId[1] == 'r' && shaderId[2] == 'n' && shaderId[3] == 'o' &&
        shaderId[4] == 'l' && shaderId[5] == 'd' && shaderId[6] == ':') {
        std::string shaderName = std::string("Arnold_") + shaderId.substr(7);
        shaderName = makeCamelCase(shaderName);
        UsdArnoldPrimReader *primReader = context.getReader()->getRegistry()->getPrimReader(shaderName);
        if (primReader) {
            primReader->read(prim, context); // read this primitive
        }
        return;
    }

    if (shaderId == "UsdPreviewSurface") {
        node = context.createArnoldNode("standard_surface", nodeName.c_str());
        
        AiNodeSetRGB(node, "base_color", 0.18f, 0.18f, 0.18f);
        exportParameter(shader, node, "diffuseColor", "base_color", context);
        AiNodeSetFlt(node, "base", 1.f); // scalar multiplier, set it to 1

        AiNodeSetRGB(node, "emission_color", 0.f, 0.f, 0.f);
        exportParameter(shader, node, "emissiveColor", "emission_color", context);
        AiNodeSetFlt(node, "emission", 1.f); // scalar multiplier, set it to 1

        UsdShadeInput paramInput = shader.GetInput(TfToken("useSpecularWorkflow"));
        int specularWorkflow = 0;
        if (paramInput) {
            paramInput.Get(&specularWorkflow);
        }

        if (specularWorkflow == 1) {
            // metallic workflow, set the specular color to white and use the
            // metalness
            AiNodeSetRGB(node, "specular_color", 1.f, 1.f, 1.f);
            exportParameter(shader, node, "metallic", "metalness", context);
        } else {
            AiNodeSetRGB(node, "specular_color", 1.f, 1.f, 1.f);
            exportParameter(shader, node, "specularColor", "specular_color", context);
            // this is actually not correct. In USD, this is apparently the
            // fresnel 0° "front-facing" specular color. Specular is considered
            // to be always white for grazing angles
        }

        AiNodeSetFlt(node, "specular_roughness", 0.5);
        exportParameter(shader, node, "roughness", "specular_roughness", context);

        AiNodeSetFlt(node, " specular_IOR  ", 1.5);
        exportParameter(shader, node, "ior", "specular_IOR", context);

        AiNodeSetFlt(node, "coat", 0.f);
        exportParameter(shader, node, "clearcoat", "coat", context);

        AiNodeSetFlt(node, "coat_roughness", 0.01f);
        exportParameter(shader, node, "clearcoatRoughness", "coat_roughness", context);

        AiNodeSetFlt(node, "opacity", 1.f);
        exportParameter(shader, node, "opacity", "opacity", context);

        UsdShadeInput normalInput = shader.GetInput(TfToken("normal"));
        if (normalInput && normalInput.HasConnectedSource()) {
            // Usd expects a tangent normal map, let's create a normal_map
            // shader, and connect it there
            std::string normalMapName = nodeName + "@normal_map";
            AtNode *normalMap = context.createArnoldNode("normal_map", normalMapName.c_str());
            AiNodeSetBool(normalMap, "color_to_signed", false);
            exportParameter(shader, normalMap, "normal", "input", context);
            AiNodeLink(normalMap, "normal", node);
        }
        // We're not exporting displacement (float) as it's part of meshes in
        // arnold. We're also not exporting the occlusion parameter (float),
        // since it doesn't really apply for arnold.

    } else if (shaderId == "UsdUVTexture") {
        node = context.createArnoldNode("image", nodeName.c_str());
        
        // Texture Shader, we want to export it as arnold "image" node
        exportParameter(shader, node, "file", "filename", context);

        // In USD, meshes don't have a "default" UV set. So we always need to
        // connect it to a user data shader.
        exportParameter(shader, node, "st", "uvcoords", context);
        exportParameter(shader, node, "fallback", "missing_texture_color", context);

        // wrapS, wrapT : "black, clamp, repeat, mirror"
        // scale
        // bias (UV offset)
    } else if (shaderId == "UsdPrimvarReader_float") {
        node = context.createArnoldNode("user_data_float", nodeName.c_str());
        exportParameter(shader, node, "varname", "attribute", context);
        exportParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_float2") {
        node = context.createArnoldNode("user_data_rgb", nodeName.c_str());
        exportParameter(shader, node, "varname", "attribute", context);
        UsdShadeInput paramInput = shader.GetInput(TfToken("fallback"));
        GfVec2f vec2Val;
        if (paramInput && paramInput.Get(&vec2Val)) {
            AiNodeSetRGB(node, "default", vec2Val[0], vec2Val[1], 0.f);
        }
    } else if (
        shaderId == "UsdPrimvarReader_float3" || shaderId == "UsdPrimvarReader_normal" ||
        shaderId == "UsdPrimvarReader_point" || shaderId == "UsdPrimvarReader_vector") {
        node = context.createArnoldNode("user_data_rgb", nodeName.c_str());
        exportParameter(shader, node, "varname", "attribute", context);
        exportParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_float4") {
        node = context.createArnoldNode("user_data_rgba", nodeName.c_str());
        exportParameter(shader, node, "varname", "attribute", context);
        exportParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_int") {
        node = context.createArnoldNode("user_data_int", nodeName.c_str());
        exportParameter(shader, node, "varname", "attribute", context);
        exportParameter(shader, node, "fallback", "default", context);
    } else if (shaderId == "UsdPrimvarReader_string") {
        node = context.createArnoldNode("user_data_string", nodeName.c_str());
        exportParameter(shader, node, "varname", "attribute", context);
        exportParameter(shader, node, "fallback", "default", context);
    } else
    {
        // support info:id = standard_surface
        std::string shaderName = std::string("Arnold_") + shaderId;
        shaderName = makeCamelCase(shaderName);
        UsdArnoldPrimReader *primReader = context.getReader()->getRegistry()->getPrimReader(shaderName);
        if (primReader) {
            primReader->read(prim, context);
        }
    }
    // User-data matrix isn't supported in arnold
    const TimeSettings &time = context.getTimeSettings();
    readArnoldParameters(prim, context, node, time);
}
