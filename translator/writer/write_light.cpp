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
#include "write_light.h"

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

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

void writeLightCommon(const AtNode *node, UsdLuxLight &light, UsdArnoldPrimWriter &primWriter, UsdArnoldWriter &writer)
{
    UsdPrim prim = light.GetPrim();
    primWriter.WriteAttribute(node, "intensity", prim, light.GetIntensityAttr(), writer);
    primWriter.WriteAttribute(node, "exposure", prim, light.GetExposureAttr(), writer);
    primWriter.WriteAttribute(node, "color", prim, light.GetColorAttr(), writer);
    primWriter.WriteAttribute(node, "diffuse", prim, light.GetDiffuseAttr(), writer);
    primWriter.WriteAttribute(node, "specular", prim, light.GetSpecularAttr(), writer);
}

} // namespace

void UsdArnoldWriteDistantLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer

    UsdLuxDistantLight light = UsdLuxDistantLight::Define(stage, SdfPath(nodeName));
    UsdPrim prim = light.GetPrim();

    WriteAttribute(node, "angle", prim, light.GetAngleAttr(), writer);
    writeLightCommon(node, light, *this, writer);
    _WriteMatrix(light, node, writer);
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteDomeLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer

    UsdLuxDomeLight light = UsdLuxDomeLight::Define(stage, SdfPath(nodeName));
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, light, *this, writer);
    _WriteMatrix(light, node, writer);

    AtNode *linkedTexture = AiNodeGetLink(node, "color");
    static AtString imageStr("image");
    if (linkedTexture && AiNodeIs(linkedTexture, imageStr)) {
        // a texture is connected to the color attribute, so we want to export it to
        // the Dome's TextureFile attribute
        AtString filename = AiNodeGetStr(linkedTexture, "filename");
        SdfAssetPath assetPath(filename.c_str());
        light.GetTextureFileAttr().Set(assetPath);
        light.GetColorAttr().ClearConnections();
        light.GetColorAttr().Set(GfVec3f(1.f, 1.f, 1.f));
        _exportedAttrs.insert("color");
    }
    AtString textureFormat = AiNodeGetStr(node, "format");
    static AtString latlongStr("latlong");
    static AtString mirrored_ballStr("mirrored_ball");
    static AtString angularStr("angular");
    if (textureFormat == latlongStr)
        light.GetTextureFormatAttr().Set(UsdLuxTokens->latlong);
    else if (textureFormat == mirrored_ballStr)
        light.GetTextureFormatAttr().Set(UsdLuxTokens->mirroredBall);
    else if (textureFormat == angularStr)
        light.GetTextureFormatAttr().Set(UsdLuxTokens->angular);

    _exportedAttrs.insert("format");

    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteDiskLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer

    UsdLuxDiskLight light = UsdLuxDiskLight::Define(stage, SdfPath(nodeName));
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, light, *this, writer);
    WriteAttribute(node, "radius", prim, light.GetRadiusAttr(), writer);
    WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);
    _WriteMatrix(light, node, writer);
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteSphereLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer

    UsdLuxSphereLight light = UsdLuxSphereLight::Define(stage, SdfPath(nodeName));
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, light, *this, writer);

    float radius = AiNodeGetFlt(node, "radius");
    if (radius > AI_EPSILON) {
        light.GetTreatAsPointAttr().Set(false);
        WriteAttribute(node, "radius", prim, light.GetRadiusAttr(), writer);
        WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);
    } else {
        light.GetTreatAsPointAttr().Set(true);
        _exportedAttrs.insert("radius");
    }

    _WriteMatrix(light, node, writer);
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteRectLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer

    UsdLuxRectLight light = UsdLuxRectLight::Define(stage, SdfPath(nodeName));
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, light, *this, writer);

    _WriteMatrix(light, node, writer);
    WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);

    AtNode *linkedTexture = AiNodeGetLink(node, "color");
    static AtString imageStr("image");
    if (linkedTexture && AiNodeIs(linkedTexture, imageStr)) {
        // a texture is connected to the color attribute, so we want to export it to
        // the Dome's TextureFile attribute
        AtString filename = AiNodeGetStr(linkedTexture, "filename");
        SdfAssetPath assetPath(filename.c_str());
        light.GetTextureFileAttr().Set(assetPath);
        light.GetColorAttr().ClearConnections();
        light.GetColorAttr().Set(GfVec3f(1.f, 1.f, 1.f));
        _exportedAttrs.insert("color");
    }

    float width = 1.f;
    float height = 1.f;

    AtArray *vertices = AiNodeGetArray(node, "vertices");
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

        light.GetWidthAttr().Set(width);
        light.GetHeightAttr().Set(height);
    }

    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteGeometryLight::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer

    UsdLuxGeometryLight light = UsdLuxGeometryLight::Define(stage, SdfPath(nodeName));
    UsdPrim prim = light.GetPrim();

    writeLightCommon(node, light, *this, writer);
    WriteAttribute(node, "normalize", prim, light.GetNormalizeAttr(), writer);
    _WriteMatrix(light, node, writer);

    AtNode *mesh = (AtNode *)AiNodeGetPtr(node, "mesh");
    if (mesh) {
        writer.WritePrimitive(mesh);
        std::string meshName = GetArnoldNodeName(mesh);
        light.CreateGeometryRel().AddTarget(SdfPath(meshName));
    }
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}
