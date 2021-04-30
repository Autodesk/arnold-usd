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
#include "read_light.h"

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
#include <pxr/usd/usdLux/shapingAPI.h>

#include <constant_strings.h>

#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

void _ReadLightCommon(const UsdLuxLight &light, AtNode *node, const TimeSettings &time)
{
    // This function is computing intensity, color, and eventually color
    // temperature. Another solution could be to export separately these
    // parameters, but it seems simpler to do this for now

    VtValue colorValue;
    GfVec3f color(1.f, 1.f, 1.f);
    if (light.GetColorAttr().Get(&colorValue, time.frame))
        color = colorValue.Get<GfVec3f>();

    VtValue intensityValue;
    float intensity = 1.f;
    if (light.GetIntensityAttr().Get(&intensityValue, time.frame)) {
        intensity = VtValueGetFloat(intensityValue);
        AiNodeSetFlt(node, str::intensity, intensity);
    }

    VtValue exposureValue;
    float exposure = 0.f;
    if (light.GetExposureAttr().Get(&exposureValue, time.frame)) {
        exposure = VtValueGetFloat(exposureValue);
        AiNodeSetFlt(node, str::exposure, exposure);
    }

    VtValue enableTemperatureValue;
    if (light.GetEnableColorTemperatureAttr().Get(&enableTemperatureValue, time.frame)) {
        if (VtValueGetBool(enableTemperatureValue)) {
            // ComputeBaseEmission will return us the combination of
            // color temperature, color, intensity and exposure.
            // But we want to ignore intensity and exposure since
            // they're already set to their corresponding attributes.
            color = light.ComputeBaseEmission();
            float colorValue = AiMax(color[0], color[1], color[2]);
            float intensityExposure = powf(2.0f, exposure) * intensity;
            if (colorValue > AI_EPSILON && intensityExposure > AI_EPSILON) {
                color /= intensityExposure;
            }
        }
    }
    AiNodeSetRGB(node, str::color, color[0], color[1], color[2]);

    VtValue diffuseValue;
    if (light.GetDiffuseAttr().Get(&diffuseValue, time.frame)) {
        AiNodeSetFlt(node, str::diffuse, VtValueGetFloat(diffuseValue));
    }
    VtValue specularValue;
    if (light.GetSpecularAttr().Get(&specularValue, time.frame)) {
        AiNodeSetFlt(node, str::specular, VtValueGetFloat(specularValue));
    }

    /*
    This is preventing distant lights from working properly, so we should only
    do it where it makes sense VtValue normalizeAttr;
    if(light.GetNormalizeAttr().Get(&normalizeAttr))
       AiNodeSetBool(node, "normalize", normalizeAttr.Get<bool>());
    */
}

// Check if some shader is linked to the light color (for skydome and quad lights only in arnold)
void _ReadLightColorLinks(const UsdLuxLight &light, AtNode *node, UsdArnoldReaderContext &context)
{
    UsdAttribute colorAttr = light.GetColorAttr();
    if (colorAttr.HasAuthoredConnections()) {
        SdfPathVector connections;
        if (colorAttr.GetConnections(&connections) && !connections.empty()) {
            // note that arnold only supports a single connection
            context.AddConnection(
                node, "color", connections[0].GetPrimPath().GetText(), 
                UsdArnoldReader::CONNECTION_LINK, connections[0].GetElementString());
        }
    }
}


AtNode *_ReadLightShaping(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    UsdLuxShapingAPI shapingAPI(prim);
    if (!shapingAPI)
        return nullptr;

    const TimeSettings &time = context.GetTimeSettings();

    VtValue coneAngleValue;
    float coneAngle = 0;
    UsdAttribute coneAngleAttr = shapingAPI.GetShapingConeAngleAttr();

    if (coneAngleAttr.HasAuthoredValue() && coneAngleAttr.Get(&coneAngleValue, time.frame))
        coneAngle = VtValueGetFloat(coneAngleValue);

    std::string iesFile;
    VtValue iesFileValue;
    UsdAttribute iesFileAttr = shapingAPI.GetShapingIesFileAttr();
    if (iesFileAttr.Get(&iesFileValue, time.frame))
        iesFile = VtValueGetString(iesFileValue);
    
    // If the cone angle is non-null, we export this light as a spot light
    if (coneAngle > AI_EPSILON) {
        AtNode *node = context.CreateArnoldNode("spot_light", prim.GetPath().GetText());
        coneAngle *= 2.f; // there's a factor of 2 between usd cone angle and arnold's one

        AiNodeSetFlt(node, str::cone_angle, coneAngle);
        VtValue shapingConeSoftnessValue;
        if (shapingAPI.GetShapingConeSoftnessAttr().Get(&shapingConeSoftnessValue, time.frame))
            AiNodeSetFlt(node, str::penumbra_angle, coneAngle * VtValueGetFloat(shapingConeSoftnessValue));

        VtValue shapingFocusValue;
        if (shapingAPI.GetShapingFocusAttr().Get(&shapingFocusValue, time.frame))
            AiNodeSetFlt(node, str::cosine_power, VtValueGetFloat(shapingFocusValue));
        return node;
    }

    // If we have a IES filename, let's export this light as a photometric light
    if (!iesFile.empty()) {
        AtNode *node = context.CreateArnoldNode("photometric_light", prim.GetPath().GetText());
        AiNodeSetStr(node, str::filename, iesFile.c_str());
        return node;
    }
    return nullptr;

}


} // namespace

void UsdArnoldReadDistantLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("distant_light", prim.GetPath().GetText());
    UsdLuxDistantLight light(prim);
    const TimeSettings &time = context.GetTimeSettings();

    float angle = 0.52f;
    VtValue angleValue;
    if (light.GetAngleAttr().Get(&angleValue, time.frame)) {
        AiNodeSetFlt(node, str::angle, VtValueGetFloat(angleValue));
    }

    _ReadLightCommon(light, node, time);
    ReadMatrix(prim, node, time, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);
}

void UsdArnoldReadDomeLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("skydome_light", prim.GetPath().GetText());

    UsdLuxDomeLight light(prim);

    // TODO : portal
    const TimeSettings &time = context.GetTimeSettings();
    _ReadLightCommon(light, node, time);    

    VtValue textureFileValue;
    if (light.GetTextureFileAttr().Get(&textureFileValue, time.frame)) {
        std::string filename = VtValueGetString(textureFileValue);
        if (!filename.empty()) {
            // there's a texture filename, so we need to connect it to the color
            std::string imageName(prim.GetPath().GetText());
            imageName += "/texture_file";
            AtNode *image = context.CreateArnoldNode("image", imageName.c_str());

            AiNodeSetStr(image, str::filename, filename.c_str());
            AiNodeLink(image, str::color, node);

            // now we need to export the intensity and exposure manually,
            // because we have overridden the color

            VtValue intensityValue;
            if (light.GetIntensityAttr().Get(&intensityValue, time.frame))
                AiNodeSetFlt(node, str::intensity, VtValueGetFloat(intensityValue));
            VtValue exposureValue;
            if (light.GetExposureAttr().Get(&exposureValue, time.frame))
                AiNodeSetFlt(node, str::exposure, VtValueGetFloat(exposureValue));
        }
    }
    TfToken format;
    if (light.GetTextureFormatAttr().Get(&format, time.frame)) {
        if (format == UsdLuxTokens->latlong) {
            AiNodeSetStr(node, str::format, str::latlong);
        } else if (format == UsdLuxTokens->mirroredBall) {
            AiNodeSetStr(node, str::format, str::mirrored_ball);
        } else if (format == UsdLuxTokens->angular) {
            AiNodeSetStr(node, str::format, str::angular);
        }
    }

    // Special case, the attribute "color" can be linked to some shader
    _ReadLightColorLinks(light, node, context);

    ReadMatrix(prim, node, time, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);
}

void UsdArnoldReadDiskLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("disk_light", prim.GetPath().GetText());

    UsdLuxDiskLight light(prim);

    const TimeSettings &time = context.GetTimeSettings();

    _ReadLightCommon(light, node, time);

    VtValue radiusValue;
    if (light.GetRadiusAttr().Get(&radiusValue, time.frame)) {
        AiNodeSetFlt(node, str::radius, VtValueGetFloat(radiusValue));
    }

    VtValue normalizeValue;
    if (light.GetNormalizeAttr().Get(&normalizeValue, time.frame)) {
        AiNodeSetBool(node, str::normalize, VtValueGetBool(normalizeValue));
    }

    ReadMatrix(prim, node, time, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);
}

// Sphere lights get exported to arnold as a point light with a radius
void UsdArnoldReadSphereLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = _ReadLightShaping(prim, context);
    if (node == nullptr)
        node = context.CreateArnoldNode("point_light", prim.GetPath().GetText());

    const TimeSettings &time = context.GetTimeSettings();
    UsdLuxSphereLight light(prim);
    _ReadLightCommon(light, node, time);

    bool treatAsPoint = false;
    VtValue treatAsPointValue;
    if (light.GetTreatAsPointAttr().Get(&treatAsPointValue, time.frame)) {
        treatAsPoint = VtValueGetBool(treatAsPointValue);
        if (!treatAsPoint) {
            VtValue radiusValue;
            if (light.GetRadiusAttr().Get(&radiusValue, time.frame)) {
                AiNodeSetFlt(node, str::radius, VtValueGetFloat(radiusValue));
            }

            VtValue normalizeValue;
            if (light.GetNormalizeAttr().Get(&normalizeValue, time.frame)) {
                AiNodeSetBool(node, str::normalize, VtValueGetBool(normalizeValue));
            }
        }
    }

    ReadMatrix(prim, node, time, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);
}

void UsdArnoldReadRectLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("quad_light", prim.GetPath().GetText());

    const TimeSettings &time = context.GetTimeSettings();

    UsdLuxRectLight light(prim);
    _ReadLightCommon(light, node, time);

    float width = 1.f;
    float height = 1.f;
    VtValue widthValue, heightValue;

    if (light.GetWidthAttr().Get(&widthValue, time.frame))
        width = VtValueGetFloat(widthValue);
    if (light.GetHeightAttr().Get(&heightValue, time.frame))
        height = VtValueGetFloat(heightValue);

    width /= 2.f;
    height /= 2.f;

    AtVector vertices[4];
    vertices[3] = AtVector(width, height, 0);
    vertices[0] = AtVector(width, -height, 0);
    vertices[1] = AtVector(-width, -height, 0);
    vertices[2] = AtVector(-width, height, 0);
    AiNodeSetArray(node, str::vertices, AiArrayConvert(4, 1, AI_TYPE_VECTOR, vertices));

    VtValue textureFileValue;
    if (light.GetTextureFileAttr().Get(&textureFileValue, time.frame)) {
        std::string filename = VtValueGetString(textureFileValue);
        if (!filename.empty()) {
            // there's a texture filename, so we need to connect it to the color
            std::string imageName(prim.GetPath().GetText());
            imageName += "/texture_file";
            AtNode *image = context.CreateArnoldNode("image", imageName.c_str());

            AiNodeSetStr(image, str::filename, filename.c_str());
            AiNodeLink(image, str::color, node);

            // now we need to export the intensity and exposure manually,
            // because we have overridden the color
            VtValue intensityValue;
            if (light.GetIntensityAttr().Get(&intensityValue, time.frame))
                AiNodeSetFlt(node, str::intensity, VtValueGetFloat(intensityValue));
            VtValue exposureValue;
            if (light.GetExposureAttr().Get(&exposureValue, time.frame))
                AiNodeSetFlt(node, str::exposure, VtValueGetFloat(exposureValue));
        }
    }
    // Special case, the attribute "color" can be linked to some shader
    _ReadLightColorLinks(light, node, context);

    VtValue normalizeValue;
    if (light.GetNormalizeAttr().Get(&normalizeValue, time.frame)) {
        AiNodeSetBool(node, str::normalize, VtValueGetBool(normalizeValue));
    }

    ReadMatrix(prim, node, time, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);
}

void UsdArnoldReadGeometryLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    // First check if the target geometry is indeed a mesh, otherwise this won't
    // work
    UsdLuxGeometryLight light(prim);

    const TimeSettings &time = context.GetTimeSettings();

    UsdRelationship rel = light.GetGeometryRel();
    SdfPathVector targets;
    rel.GetTargets(&targets);
    if (targets.empty()) {
        return;
    }

    // Need to export one mesh_light per target geometry
    for (size_t i = 0; i < targets.size(); ++i) {
        const SdfPath &geomPath = targets[i];
        // Should we instead call Load ?
        UsdPrim targetPrim = context.GetReader()->GetStage()->GetPrimAtPath(geomPath);
        if (!targetPrim.IsA<UsdGeomMesh>()) {
            continue; // arnold's mesh lights only support meshes
        }

        AtNode *node = nullptr;
        std::string lightName = prim.GetPath().GetText();
        if (i > 0) {
            lightName += std::string("_") + std::string(targetPrim.GetPath().GetText());
        }
        node = context.CreateArnoldNode("mesh_light", lightName.c_str());
        context.AddConnection(node, "mesh", targetPrim.GetPath().GetText(), UsdArnoldReader::CONNECTION_PTR);

        _ReadLightCommon(light, node, time);

        VtValue normalizeValue;
        if (light.GetNormalizeAttr().Get(&normalizeValue, time.frame)) {
            AiNodeSetBool(node, str::normalize, VtValueGetBool(normalizeValue));
        }
        // Special case, the attribute "color" can be linked to some shader
        _ReadLightColorLinks(light, node, context);

        ReadMatrix(prim, node, time, context);
        _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

        // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
        if (!context.GetPrimVisibility(prim, time.frame))
            AiNodeSetFlt(node, str::intensity, 0.f);
    }
}
