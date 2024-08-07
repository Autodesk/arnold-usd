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
#include "write_light.h"
#include <constant_strings.h>

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/geometryLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

UsdPrim _GetNodeGraph(UsdPrim& prim, UsdArnoldWriter &writer, const TfToken& lightShaderAttr)
{
    std::string nodeGraphName = prim.GetPath().GetString() + "/light_shader";
    SdfPath nodeGraphPath(nodeGraphName);        
    UsdStageRefPtr stage = writer.GetUsdStage();
    UsdPrim nodeGraphPrim = stage->DefinePrim(nodeGraphPath, str::t_ArnoldNodeGraph);

    UsdAttribute arnoldShaderAttr = 
        prim.CreateAttribute(lightShaderAttr, 
        SdfValueTypeNames->String, false);
    arnoldShaderAttr.Set(nodeGraphPrim.GetPath().GetString());

    return nodeGraphPrim;
}

bool _WriteArnoldLightShader(const AtNode* node, UsdPrim& prim, 
    UsdArnoldPrimWriter &primWriter, UsdArnoldWriter &writer, const TfToken& lightShaderAttr)
{
    AtNode *linkedColor = AiNodeGetLink(node, str::color);
    
    if (!linkedColor)
        return false;
    
    UsdPrim nodeGraphPrim = _GetNodeGraph(prim, writer, lightShaderAttr);
    
    primWriter.WriteAttribute(node, "color", nodeGraphPrim, 
        nodeGraphPrim.CreateAttribute(str::t_outputs_color, 
            SdfValueTypeNames->Token, false), writer);
    return true;
}

bool _WriteArnoldLightFilters(const AtNode* node, UsdPrim& prim, 
    UsdArnoldPrimWriter &primWriter, UsdArnoldWriter &writer, const TfToken& lightShaderAttr)
{
    const AtArray *lightFilters = AiNodeGetArray(node, str::filters);    
    unsigned int numFilters = lightFilters ? AiArrayGetNumElements(lightFilters) : 0;

    if (numFilters == 0)
        return false;

    UsdPrim nodeGraphPrim = _GetNodeGraph(prim, writer, lightShaderAttr);
    for (unsigned int i = 0; i < numFilters; ++i) {
        AtNode* filter = (AtNode*)AiArrayGetPtr(lightFilters, i);
        if (filter == nullptr)
            continue;

        TfToken filterIndexAttrName(TfStringPrintf("outputs:filters:i%d", i+1));
        UsdAttribute filterIndexAttr = nodeGraphPrim.CreateAttribute(filterIndexAttrName, 
            SdfValueTypeNames->Token, false);
        
        writer.WritePrimitive(filter);
        std::string filterName = UsdArnoldPrimWriter::GetArnoldNodeName(filter, writer);
        UsdPrim filterPrim = writer.GetUsdStage()->GetPrimAtPath(SdfPath(filterName));
        UsdAttribute filterOutputAttr = filterPrim.CreateAttribute(str::t_outputs_out, SdfValueTypeNames->Token, false);
        filterName += ".outputs:out";
        
        SdfPath filterOutputPath(filterName);
        filterIndexAttr.AddConnection(filterOutputPath);
    }
    return true;
}


void writeLightCommon(const AtNode *node, UsdPrim &prim, UsdArnoldPrimWriter &primWriter, UsdArnoldWriter &writer)
{
#if PXR_VERSION >= 2111
    UsdLuxLightAPI light(prim);
#else
    UsdLuxLight light(prim);
#endif
    
    primWriter.WriteAttribute(node, "intensity", prim, light.GetIntensityAttr(), writer);
    primWriter.WriteAttribute(node, "exposure", prim, light.GetExposureAttr(), writer);
    primWriter.WriteAttribute(node, "color", prim, light.GetColorAttr(), writer);
    primWriter.WriteAttribute(node, "diffuse", prim, light.GetDiffuseAttr(), writer);
    primWriter.WriteAttribute(node, "specular", prim, light.GetSpecularAttr(), writer);
}

} // namespace

void UsdArnoldWriteDistantLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);        
    writer.CreateHierarchy(objPath);
    UsdLuxDistantLight light = UsdLuxDistantLight::Define(stage, objPath);
    UsdPrim prim = light.GetPrim();

    WriteAttribute(node, "angle", prim, light.GetAngleAttr(), writer);
    writeLightCommon(node, prim, *this, writer);
    _WriteMatrix(light, node, writer);

    UsdAttribute normalizeAttr = prim.CreateAttribute(TfToken("primvars:arnold:normalize"), SdfValueTypeNames->Bool, false);
    WriteAttribute(node, "normalize", prim, normalizeAttr, writer);
    
    _WriteArnoldLightFilters(node, prim, *this, writer, str::t_primvars_arnold_shaders);
    _exportedAttrs.insert("filters");
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteDomeLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);
    writer.CreateHierarchy(objPath);
    UsdLuxDomeLight light = UsdLuxDomeLight::Define(stage, objPath);
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, prim, *this, writer);
    _WriteMatrix(light, node, writer);
    
    if (_WriteArnoldLightShader(node, prim, *this, writer, str::t_primvars_arnold_shaders)) {
        _exportedAttrs.insert("color");
    
        AtNode *linkedTexture = AiNodeGetLink(node, "color");
        static AtString imageStr("image");
        if (linkedTexture && AiNodeIs(linkedTexture, imageStr)) {
            // a texture is connected to the color attribute, so we want to export it to
            // the Dome's TextureFile attribute
            AtString filename = AiNodeGetStr(linkedTexture, AtString("filename"));
            SdfAssetPath assetPath(filename.c_str());
            writer.SetAttribute(light.GetTextureFileAttr(), assetPath);
            light.GetColorAttr().ClearConnections();
            writer.SetAttribute(light.GetColorAttr(), GfVec3f(1.f, 1.f, 1.f));
        }
    }
    _WriteArnoldLightFilters(node, prim, *this, writer, str::t_primvars_arnold_shaders);
    _exportedAttrs.insert("filters");
 
    AtString textureFormat = AiNodeGetStr(node, AtString("format"));
    static AtString latlongStr("latlong");
    static AtString mirrored_ballStr("mirrored_ball");
    static AtString angularStr("angular");
    if (textureFormat == latlongStr)
        writer.SetAttribute(light.GetTextureFormatAttr(), UsdLuxTokens->latlong);
    else if (textureFormat == mirrored_ballStr)
        writer.SetAttribute(light.GetTextureFormatAttr(), UsdLuxTokens->mirroredBall);
    else if (textureFormat == angularStr)
        writer.SetAttribute(light.GetTextureFormatAttr(), UsdLuxTokens->angular);

    _exportedAttrs.insert("format");

    UsdAttribute normalizeAttr = prim.CreateAttribute(str::t_primvars_arnold_normalize, SdfValueTypeNames->Bool, false);
    WriteAttribute(node, "normalize", prim, normalizeAttr, writer);

    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteDiskLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);    
    writer.CreateHierarchy(objPath);
    UsdLuxDiskLight light = UsdLuxDiskLight::Define(stage, objPath);
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, prim, *this, writer);
    WriteAttribute(node, "radius", prim, light.GetRadiusAttr(), writer);
    WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);
    _WriteMatrix(light, node, writer);
    _WriteArnoldLightFilters(node, prim, *this, writer, str::t_primvars_arnold_shaders);
    _exportedAttrs.insert("filters");
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteSphereLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);    
    writer.CreateHierarchy(objPath);
    UsdLuxSphereLight light = UsdLuxSphereLight::Define(stage, objPath);
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, prim, *this, writer);

    float radius = AiNodeGetFlt(node, AtString("radius"));
    if (radius > AI_EPSILON) {
        writer.SetAttribute(light.GetTreatAsPointAttr(), false);
        WriteAttribute(node, "radius", prim, light.GetRadiusAttr(), writer);
        WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);
    } else {
        writer.SetAttribute(light.GetTreatAsPointAttr(), true);
        _exportedAttrs.insert("radius");
    }

    _WriteMatrix(light, node, writer);
    _WriteArnoldLightFilters(node, prim, *this, writer, str::t_primvars_arnold_shaders);
    _exportedAttrs.insert("filters");    
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteRectLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);    
    writer.CreateHierarchy(objPath);
    UsdLuxRectLight light = UsdLuxRectLight::Define(stage, objPath);
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, prim, *this, writer);

    _WriteMatrix(light, node, writer);
    WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);

    if (_WriteArnoldLightShader(node, prim, *this, writer, str::t_primvars_arnold_shaders)) {
        _exportedAttrs.insert("color");

        AtNode *linkedTexture = AiNodeGetLink(node, AtString("color"));
        static AtString imageStr("image");
        if (linkedTexture && AiNodeIs(linkedTexture, imageStr)) {
            // a texture is connected to the color attribute, so we want to export it to
            // the Dome's TextureFile attribute
            AtString filename = AiNodeGetStr(linkedTexture, AtString("filename"));
            SdfAssetPath assetPath(filename.c_str());
            writer.SetAttribute(light.GetTextureFileAttr(), assetPath);
            light.GetColorAttr().ClearConnections();
            writer.SetAttribute(light.GetColorAttr(), GfVec3f(1.f, 1.f, 1.f));        
        }
    }
    _WriteArnoldLightFilters(node, prim, *this, writer, str::t_primvars_arnold_shaders);
    _exportedAttrs.insert("filters");

    float width = 1.f;
    float height = 1.f;

    AtArray *vertices = AiNodeGetArray(node, AtString("vertices"));
    if (vertices && AiArrayGetNumElements(vertices) >= 4) {
        // Note that we can only export the simplest case to USD.
        // Since the arnold attribute allows to do more than UsdLuxRectLight,
        // we're not adding "vertices" to the list of exported nodes, so that it will
        // also be exported with the arnold: namespace (if not default)

        AtVector vertexPos[4];
        AtVector maxVertex(-AI_INFINITE, -AI_INFINITE, -AI_INFINITE);
        AtVector minVertex(AI_INFINITE, AI_INFINITE, AI_INFINITE);
        for (int i = 0; i < 4; ++i) {
            vertexPos[i] = AiArrayGetVec(vertices, i);
            maxVertex.x = AiMax(maxVertex.x, vertexPos[i].x);
            minVertex.x = AiMin(minVertex.x, vertexPos[i].x);
            maxVertex.y = AiMax(maxVertex.y, vertexPos[i].y);
            minVertex.y = AiMin(minVertex.y, vertexPos[i].y);
            maxVertex.z = AiMax(maxVertex.z, vertexPos[i].z);
            minVertex.z = AiMin(minVertex.z, vertexPos[i].z);
        }
        width = maxVertex.x - minVertex.x;
        height = maxVertex.y - minVertex.y;

        writer.SetAttribute(light.GetWidthAttr(), width);
        writer.SetAttribute(light.GetHeightAttr(), height);
    }

    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}
void UsdArnoldWriteCylinderLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);    
    writer.CreateHierarchy(objPath);
    UsdLuxCylinderLight light = UsdLuxCylinderLight::Define(stage, objPath);
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, prim, *this, writer);
    WriteAttribute(node, "radius", prim, light.GetRadiusAttr(), writer);

    WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);
    _WriteMatrix(light, node, writer);
    _WriteArnoldLightFilters(node, prim, *this, writer, str::t_primvars_arnold_shaders);
    _exportedAttrs.insert("filters");
       
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteGeometryLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer

    AtNode *meshNode = (AtNode *)AiNodeGetPtr(node, AtString("mesh"));
    if (!meshNode)
        return;

    writer.WritePrimitive(meshNode);
    std::string meshName = GetArnoldNodeName(meshNode, writer);
    SdfPath meshPath(meshName);
    UsdPrim mesh = stage->GetPrimAtPath(meshPath);

    UsdAttribute lightAttr = mesh.CreateAttribute(str::t_primvars_arnold_light, 
            SdfValueTypeNames->Bool, false);
    lightAttr.Set(true);

    _exportedAttrs.insert("mesh");
    // We're not authoring the light matrix, so that it's consistent with the mesh
    _exportedAttrs.insert("matrix");

    if (_WriteArnoldLightShader(node, mesh, *this, writer, str::t_primvars_arnold_light_shaders))
        _exportedAttrs.insert("color");

    _WriteArnoldLightFilters(node, mesh, *this, writer, str::t_primvars_arnold_light_shaders);
    _exportedAttrs.insert("filters");
    
    _WriteArnoldParameters(node, writer, mesh, "primvars:arnold:light");    
}

void UsdArnoldWriteSpotLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);    
    writer.CreateHierarchy(objPath);
    UsdLuxSphereLight light = UsdLuxSphereLight::Define(stage, objPath);
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, prim, *this, writer);

    float radius = AiNodeGetFlt(node, AtString("radius"));
    if (radius > AI_EPSILON) {
        writer.SetAttribute(light.GetTreatAsPointAttr(), false);
        WriteAttribute(node, "radius", prim, light.GetRadiusAttr(), writer);
        WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);
    } else {
        writer.SetAttribute(light.GetTreatAsPointAttr(), true);
        _exportedAttrs.insert("radius");
    }

    UsdLuxShapingAPI shapingAPI = UsdLuxShapingAPI::Apply(prim);
    float coneAngle = AiNodeGetFlt(node, AtString("cone_angle"));
    writer.SetAttribute(shapingAPI.CreateShapingConeAngleAttr(), coneAngle / 2.f);
    
    _exportedAttrs.insert("cone_angle");
    float shapingConeSoftness = AiNodeGetFlt(node, AtString("penumbra_angle"));
    if (coneAngle > AI_EPSILON)
        shapingConeSoftness /= coneAngle;
    writer.SetAttribute(shapingAPI.CreateShapingConeSoftnessAttr(), shapingConeSoftness);
    _exportedAttrs.insert("penumbra_angle");

    WriteAttribute(node, "cosine_power", prim, shapingAPI.CreateShapingFocusAttr(), writer);

    _WriteMatrix(light, node, writer);
    _WriteArnoldLightFilters(node, prim, *this, writer, str::t_primvars_arnold_shaders);
    _exportedAttrs.insert("filters");    
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWritePhotometricLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);    
    writer.CreateHierarchy(objPath);
    UsdLuxSphereLight light = UsdLuxSphereLight::Define(stage, objPath);
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, prim, *this, writer);

    float radius = AiNodeGetFlt(node, AtString("radius"));
    if (radius > AI_EPSILON) {
        writer.SetAttribute(light.GetTreatAsPointAttr(), false);
        WriteAttribute(node, "radius", prim, light.GetRadiusAttr(), writer);
        WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);
    } else {
        writer.SetAttribute(light.GetTreatAsPointAttr(), true);
        _exportedAttrs.insert("radius");
    }
    UsdLuxShapingAPI shapingAPI = UsdLuxShapingAPI::Apply(prim);
    AtString filename = AiNodeGetStr(node, str::filename);
    if (!filename.empty()) {
        SdfAssetPath assetPath(filename.c_str());
        writer.SetAttribute(shapingAPI.CreateShapingIesFileAttr(), assetPath);
    }
    _exportedAttrs.insert("filename");    

    _WriteMatrix(light, node, writer);
    _WriteArnoldLightFilters(node, prim, *this, writer, str::t_primvars_arnold_shaders);
    _exportedAttrs.insert("filters");    
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}
