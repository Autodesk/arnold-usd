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
#include "read_light.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdLux/blackbody.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/geometryLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/shadowAPI.h>

#include <constant_strings.h>
#include <parameters_utils.h>

#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE
// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((Angle, "angle"))
    (ArnoldNodeGraph)
    ((Color, "color"))
    ((ColorTemperature, "colorTemperature"))
    ((Diffuse, "diffuse"))
    ((EnableColorTemperature, "enableColorTemperature"))
    ((Exposure, "exposure"))
    ((Height, "height"))
    ((Intensity, "intensity"))
    ((Length, "length"))
    ((Normalize, "normalize"))
    ((PrimvarsArnoldShaders, "primvars:arnold:shaders"))
    ((Radius, "radius"))
    ((ShadowColor, "shadow:color"))
    ((ShadowDistance, "shadow:distance"))
    ((ShadowEnable, "shadow:enable"))
    ((ShadowFalloff, "shadow:falloff"))
    ((ShadowFalloffGamma, "shadow:falloffGamma"))
    ((ShapingConeAngle, "shaping:cone:angle"))
    ((ShapingConeSoftness, "shaping:cone:softness"))
    ((ShapingFocus, "shaping:focus"))
    ((ShapingFocusTint, "shaping:focusTint"))
    ((ShapingIesAngleScale, "shaping:ies:angleScale"))
    ((ShapingIesFile, "shaping:ies:file"))
    ((ShapingIesNormalize, "shaping:ies:normalize"))
    ((Specular, "specular"))
    ((TextureFile, "texture:file"))
    ((TextureFormat, "texture:format"))
    ((Width, "width"))
);
// clang-format on

namespace {

template <typename T>
UsdAttribute _GetLightAttr(const T& light, const UsdAttribute& attr, const TfToken& oldName)
{
    if (attr.HasAuthoredValue()) {
        return attr;
    } else {
        auto oldAttr = light.GetPrim().GetAttribute(oldName);
        return oldAttr && oldAttr.HasAuthoredValue() ? oldAttr : attr;
    }
}

template <typename T>
UsdAttribute _GetLightAttrConnections(const T& light, const UsdAttribute& attr, const TfToken& oldName)
{
    if (attr.HasAuthoredConnections()) {
        return attr;
    } else {
        auto oldAttr = light.GetPrim().GetAttribute(oldName);
        return oldAttr && oldAttr.HasAuthoredConnections() ? oldAttr : attr;
    }
}

#if 1
#define GET_LIGHT_ATTR(l, a) _GetLightAttr(l, l.Get ## a ## Attr(), _tokens->a)
#define GET_LIGHT_ATTR_CONNS(l, a) _GetLightAttrConnections(l, l.Get ## a ## Attr(), _tokens->a)
#else
#define GET_LIGHT_ATTR(l, a) l.Get ## a ## Attr()
#define GET_LIGHT_ATTR_CONNS(l, a) l.Get ## a ## Attr()
#endif

void _ReadLightCommon(const UsdPrim& prim, AtNode *node, const TimeSettings &time)
{
#if PXR_VERSION >= 2111
    UsdLuxLightAPI light(prim);
#else
    UsdLuxLight light(prim);
#endif
    
    // This function is computing intensity, color, and eventually color
    // temperature. Another solution could be to export separately these
    // parameters, but it seems simpler to do this for now

    VtValue colorValue;
    GfVec3f color(1.f, 1.f, 1.f);
    if (GET_LIGHT_ATTR(light, Color).Get(&colorValue, time.frame))
        color = colorValue.Get<GfVec3f>();

    VtValue intensityValue;
    float intensity = 1.f;
    if (GET_LIGHT_ATTR(light, Intensity).Get(&intensityValue, time.frame)) {
        intensity = VtValueGetFloat(intensityValue);
        AiNodeSetFlt(node, str::intensity, intensity);
    }

    VtValue exposureValue;
    float exposure = 0.f;
    if (GET_LIGHT_ATTR(light, Exposure).Get(&exposureValue, time.frame)) {
        exposure = VtValueGetFloat(exposureValue);
        AiNodeSetFlt(node, str::exposure, exposure);
    }

    VtValue enableTemperatureValue;
    if (GET_LIGHT_ATTR(light, EnableColorTemperature).Get(&enableTemperatureValue, time.frame)) {
        float colorTemp = 6500;
        if (VtValueGetBool(enableTemperatureValue) &&
            GET_LIGHT_ATTR(light, ColorTemperature).Get(&colorTemp, time.frame)) {
            color = GfCompMult(color, UsdLuxBlackbodyTemperatureAsRgb(colorTemp));
        }
    }
    AiNodeSetRGB(node, str::color, color[0], color[1], color[2]);

    VtValue diffuseValue;
    if (GET_LIGHT_ATTR(light, Diffuse).Get(&diffuseValue, time.frame)) {
        AiNodeSetFlt(node, str::diffuse, VtValueGetFloat(diffuseValue));
    }
    VtValue specularValue;
    if (GET_LIGHT_ATTR(light, Specular).Get(&specularValue, time.frame)) {
        AiNodeSetFlt(node, str::specular, VtValueGetFloat(specularValue));
    }

    /*
    This is preventing distant lights from working properly, so we should only
    do it where it makes sense VtValue normalizeAttr.
    if(light.GetNormalizeAttr().Get(&normalizeAttr, time.frame))
       AiNodeSetBool(node, "normalize", normalizeAttr.Get<bool>());
    */

    UsdLuxShadowAPI shadowAPI(prim);
    if (shadowAPI) {
        VtValue shadowEnableValue;
        if (GET_LIGHT_ATTR(shadowAPI, ShadowEnable).Get(&shadowEnableValue, time.frame)) {
            AiNodeSetBool(node, str::cast_shadows, VtValueGetBool(shadowEnableValue));
        }
        VtValue shadowColorValue;
        if (GET_LIGHT_ATTR(shadowAPI, ShadowColor).Get(&shadowColorValue, time.frame)) {
            GfVec3f rgb = VtValueGetVec3f(shadowColorValue);
            AiNodeSetRGB(node, str::shadow_color, rgb[0], rgb[1], rgb[2]);
        }
    }  

}

void _ReadLightLinks(const UsdPrim &prim, AtNode *node, UsdArnoldReaderContext &context)
{
#if PXR_VERSION >= 2111
    UsdLuxLightAPI light(prim);
#else
    UsdLuxLight light(prim);
#endif
    
    UsdCollectionAPI lightLinkCollection = light.GetLightLinkCollectionAPI();
    if (lightLinkCollection) {
        VtValue lightIncludeRootValue;
        bool lightIncludeRoot = (lightLinkCollection.GetIncludeRootAttr().Get(&lightIncludeRootValue)) ? VtValueGetBool(lightIncludeRootValue) : false;
        UsdRelationship lightExcludeRel = lightLinkCollection.GetExcludesRel();
        if (!lightIncludeRoot  || lightExcludeRel.HasAuthoredTargets()) {
            // we have an explicit list of geometries for this light
            context.RegisterLightLinks(AiNodeGetName(node), lightLinkCollection);
        }
    }

    UsdCollectionAPI shadowLinkCollection = light.GetShadowLinkCollectionAPI();
    if (shadowLinkCollection) {
        VtValue shadowIncludeRootValue;
        bool shadowIncludeRoot = (shadowLinkCollection.GetIncludeRootAttr().Get(&shadowIncludeRootValue)) ? VtValueGetBool(shadowIncludeRootValue) : false;
        UsdRelationship shadowExcludeRel = shadowLinkCollection.GetExcludesRel();
        if (!shadowIncludeRoot  || shadowExcludeRel.HasAuthoredTargets()) {
            // we have an explicit list of geometries for this light's shadows
            context.RegisterShadowLinks(AiNodeGetName(node), shadowLinkCollection);
        }
    }
}
// Check if some shader is linked to the light color (for skydome and quad lights only in arnold)
void _ReadLightColorLinks(const UsdPrim& prim, AtNode *node, UsdArnoldReaderContext &context)
{
#if PXR_VERSION >= 2111
    UsdLuxLightAPI light(prim);
#else
    UsdLuxLight light(prim);
#endif
    
    UsdAttribute colorAttr = GET_LIGHT_ATTR_CONNS(light, Color);
    if (colorAttr.HasAuthoredConnections()) {
        SdfPathVector connections;
        if (colorAttr.GetConnections(&connections) && !connections.empty()) {
            // note that arnold only supports a single connection
            context.AddConnection(
                node, "color", connections[0].GetPrimPath().GetText(),
                ArnoldAPIAdapter::CONNECTION_LINK, connections[0].GetElementString());
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

    if (coneAngleAttr.HasAuthoredValue()) {
        if (coneAngleAttr.Get(&coneAngleValue, time.frame)) {
            coneAngle = VtValueGetFloat(coneAngleValue);
        }
    } else {
        coneAngleAttr = prim.GetAttribute(_tokens->ShapingConeAngle);
        if (coneAngleAttr.HasAuthoredValue() && coneAngleAttr.Get(&coneAngleValue, time.frame)) {
            coneAngle = VtValueGetFloat(coneAngleValue);
        }
    }

    std::string iesFile;
    VtValue iesFileValue;
    UsdAttribute iesFileAttr = shapingAPI.GetShapingIesFileAttr();
    if (GET_LIGHT_ATTR(shapingAPI, ShapingIesFile).Get(&iesFileValue, time.frame)) {
        iesFile = VtValueGetString(iesFileValue);
    }

    // First, if we have a IES filename, let's export this light as a photometric light (#1316)
    if (!iesFile.empty()) {
        AtNode *node = context.CreateArnoldNode("photometric_light", prim.GetPath().GetText());
        AiNodeSetStr(node, str::filename, AtString(iesFile.c_str()));
        return node;
    }
    
    // If the cone angle is non-null, we export this light as a spot light
    if (coneAngle > AI_EPSILON) {
        AtNode *node = context.CreateArnoldNode("spot_light", prim.GetPath().GetText());
        coneAngle *= 2.f; // there's a factor of 2 between usd cone angle and arnold's one

        AiNodeSetFlt(node, str::cone_angle, coneAngle);
        VtValue shapingConeSoftnessValue;
        if (GET_LIGHT_ATTR(shapingAPI, ShapingConeSoftness).Get(&shapingConeSoftnessValue, time.frame))
            AiNodeSetFlt(node, str::penumbra_angle, coneAngle * VtValueGetFloat(shapingConeSoftnessValue));

        VtValue shapingFocusValue;
        if (GET_LIGHT_ATTR(shapingAPI, ShapingFocus).Get(&shapingFocusValue, time.frame))
            AiNodeSetFlt(node, str::cosine_power, VtValueGetFloat(shapingFocusValue));
        return node;
    }

    return nullptr;

}


} // namespace

AtNode* UsdArnoldReadDistantLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("distant_light", prim.GetPath().GetText());
    UsdLuxDistantLight light(prim);
    const TimeSettings &time = context.GetTimeSettings();

    VtValue angleValue;
    if (GET_LIGHT_ATTR(light, Angle).Get(&angleValue, time.frame)) {
        AiNodeSetFlt(node, str::angle, VtValueGetFloat(angleValue));
    }

    _ReadLightCommon(prim, node, time);
    ReadMatrix(prim, node, time, context);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);

    _ReadLightLinks(prim, node, context);
    ReadLightShaders(prim, prim.GetAttribute(_tokens->PrimvarsArnoldShaders), node, context);
    return node;
}

AtNode* UsdArnoldReadDomeLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("skydome_light", prim.GetPath().GetText());

    UsdLuxDomeLight light(prim);

    // TODO : portal
    const TimeSettings &time = context.GetTimeSettings();
    _ReadLightCommon(prim, node, time);

    VtValue textureFileValue;
    if (GET_LIGHT_ATTR(light, TextureFile).Get(&textureFileValue, time.frame)) {
        UsdAttribute attr = light.GetTextureFileAttr();
        std::string filename = VtValueGetString(textureFileValue);
        if (!filename.empty()) {
            // there's a texture filename, so we need to connect it to the color
            std::string imageName(prim.GetPath().GetText());
            imageName += "/texture_file";
            AtNode *image = context.CreateArnoldNode("image", imageName.c_str());

            AiNodeSetStr(image, str::filename, AtString(filename.c_str()));
            AiNodeLink(image, str::color, node);

            // now we need to export the intensity and exposure manually,
            // because we have overridden the color

            VtValue intensityValue;
            if (GET_LIGHT_ATTR(light, Intensity).Get(&intensityValue, time.frame))
                AiNodeSetFlt(node, str::intensity, VtValueGetFloat(intensityValue));
            VtValue exposureValue;
            if (GET_LIGHT_ATTR(light, Exposure).Get(&exposureValue, time.frame))
                AiNodeSetFlt(node, str::exposure, VtValueGetFloat(exposureValue));
        }
    }
    TfToken format;
    if (GET_LIGHT_ATTR(light, TextureFormat).Get(&format, time.frame)) {
        if (format == UsdLuxTokens->latlong) {
            AiNodeSetStr(node, str::format, str::latlong);
        } else if (format == UsdLuxTokens->mirroredBall) {
            AiNodeSetStr(node, str::format, str::mirrored_ball);
        } else if (format == UsdLuxTokens->angular) {
            AiNodeSetStr(node, str::format, str::angular);
        }
    }

    // Special case, the attribute "color" can be linked to some shader
    _ReadLightColorLinks(prim, node, context);

    ReadMatrix(prim, node, time, context);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);

    _ReadLightLinks(prim, node, context);
    ReadLightShaders(prim, prim.GetAttribute(_tokens->PrimvarsArnoldShaders), node, context);
    return node;
}

AtNode* UsdArnoldReadDiskLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("disk_light", prim.GetPath().GetText());

    UsdLuxDiskLight light(prim);

    const TimeSettings &time = context.GetTimeSettings();

    _ReadLightCommon(prim, node, time);

    VtValue radiusValue;
    if (GET_LIGHT_ATTR(light, Radius).Get(&radiusValue, time.frame)) {
        AiNodeSetFlt(node, str::radius, VtValueGetFloat(radiusValue));
    }

    VtValue normalizeValue;
    if (GET_LIGHT_ATTR(light, Normalize).Get(&normalizeValue, time.frame)) {
        AiNodeSetBool(node, str::normalize, VtValueGetBool(normalizeValue));
    }

    ReadMatrix(prim, node, time, context);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);

    _ReadLightLinks(prim, node, context);
    ReadLightShaders(prim, prim.GetAttribute(_tokens->PrimvarsArnoldShaders), node, context);
    return node;
}

// Sphere lights get exported to arnold as a point light with a radius
AtNode* UsdArnoldReadSphereLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = _ReadLightShaping(prim, context);
    if (node == nullptr)
        node = context.CreateArnoldNode("point_light", prim.GetPath().GetText());

    const TimeSettings &time = context.GetTimeSettings();
    UsdLuxSphereLight light(prim);
    _ReadLightCommon(prim, node, time);

    bool treatAsPoint = false;
    VtValue treatAsPointValue;
    if (light.GetTreatAsPointAttr().Get(&treatAsPointValue, time.frame)) {
        treatAsPoint = VtValueGetBool(treatAsPointValue);
        if (!treatAsPoint) {
            VtValue radiusValue;
            if (GET_LIGHT_ATTR(light, Radius).Get(&radiusValue, time.frame)) {
                AiNodeSetFlt(node, str::radius, VtValueGetFloat(radiusValue));
            }

            VtValue normalizeValue;
            if (GET_LIGHT_ATTR(light, Normalize).Get(&normalizeValue, time.frame)) {
                AiNodeSetBool(node, str::normalize, VtValueGetBool(normalizeValue));
            }
        }
    }

    ReadMatrix(prim, node, time, context);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);

    _ReadLightLinks(prim, node, context);
    ReadLightShaders(prim, prim.GetAttribute(_tokens->PrimvarsArnoldShaders), node, context);
    return node;
}

AtNode* UsdArnoldReadRectLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("quad_light", prim.GetPath().GetText());

    const TimeSettings &time = context.GetTimeSettings();

    UsdLuxRectLight light(prim);
    _ReadLightCommon(prim, node, time);

    float width = 1.f;
    float height = 1.f;
    VtValue widthValue, heightValue;

    if (GET_LIGHT_ATTR(light, Width).Get(&widthValue, time.frame))
        width = VtValueGetFloat(widthValue);
    if (GET_LIGHT_ATTR(light, Height).Get(&heightValue, time.frame))
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
    if (GET_LIGHT_ATTR(light, TextureFile).Get(&textureFileValue, time.frame)) {
        UsdAttribute attr = light.GetTextureFileAttr();
        std::string filename = VtValueGetString(textureFileValue);
        if (!filename.empty()) {
            // there's a texture filename, so we need to connect it to the color
            std::string imageName(prim.GetPath().GetText());
            imageName += "/texture_file";
            AtNode *image = context.CreateArnoldNode("image", imageName.c_str());

            AiNodeSetStr(image, str::filename, AtString(filename.c_str()));
            AiNodeSetBool(image, str::sflip, true);
            AiNodeLink(image, str::color, node);

            // now we need to export the intensity and exposure manually,
            // because we have overridden the color
            VtValue intensityValue;
            if (GET_LIGHT_ATTR(light, Intensity).Get(&intensityValue, time.frame))
                AiNodeSetFlt(node, str::intensity, VtValueGetFloat(intensityValue));
            VtValue exposureValue;
            if (GET_LIGHT_ATTR(light, Exposure).Get(&exposureValue, time.frame))
                AiNodeSetFlt(node, str::exposure, VtValueGetFloat(exposureValue));
        }
    }
    // Special case, the attribute "color" can be linked to some shader
    _ReadLightColorLinks(prim, node, context);

    VtValue normalizeValue;
    if (GET_LIGHT_ATTR(light, Normalize).Get(&normalizeValue, time.frame)) {
        AiNodeSetBool(node, str::normalize, VtValueGetBool(normalizeValue));
    }

    ReadMatrix(prim, node, time, context);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);

    _ReadLightLinks(prim, node, context);
    ReadLightShaders(prim, prim.GetAttribute(_tokens->PrimvarsArnoldShaders), node, context);
    return node;
}

AtNode* UsdArnoldReadCylinderLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("cylinder_light", prim.GetPath().GetText());

    const TimeSettings &time = context.GetTimeSettings();
    UsdLuxCylinderLight light(prim);
    _ReadLightCommon(prim, node, time);

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, str::intensity, 0.f);

    VtValue radiusValue;
    if (GET_LIGHT_ATTR(light, Radius).Get(&radiusValue, time.frame)) {
        AiNodeSetFlt(node, str::radius, VtValueGetFloat(radiusValue));
    }

    VtValue lengthValue;
    if (GET_LIGHT_ATTR(light, Length).Get(&lengthValue, time.frame)) {
        float length = VtValueGetFloat(lengthValue) / 2.f;
        AiNodeSetVec(node, str::bottom, -length, 0.0f, 0.0f);
        AiNodeSetVec(node, str::top, length, 0.0f, 0.0f);
    }
    ReadMatrix(prim, node, time, context);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");
    _ReadLightLinks(prim, node, context);
    ReadLightShaders(prim, prim.GetAttribute(_tokens->PrimvarsArnoldShaders), node, context);
    return node;
}


AtNode* UsdArnoldReadGeometryLight::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    // First check if the target geometry is indeed a mesh, otherwise this won't
    // work
    UsdLuxGeometryLight light(prim);

    const TimeSettings &time = context.GetTimeSettings();

    UsdRelationship rel = light.GetGeometryRel();
    SdfPathVector targets;
    rel.GetTargets(&targets);
    if (targets.empty()) {
        return nullptr;
    }
    AtNode* res = nullptr;

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
        if (res == nullptr)
            res = node;
        context.AddConnection(node, "mesh", targetPrim.GetPath().GetText(), ArnoldAPIAdapter::CONNECTION_PTR);

        _ReadLightCommon(prim, node, time);

        VtValue normalizeValue;
        if (GET_LIGHT_ATTR(light, Normalize).Get(&normalizeValue, time.frame)) {
            AiNodeSetBool(node, str::normalize, VtValueGetBool(normalizeValue));
        }
        // Special case, the attribute "color" can be linked to some shader
        _ReadLightColorLinks(prim, node, context);

        ReadMatrix(prim, node, time, context);
        ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

        // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
        if (!context.GetPrimVisibility(prim, time.frame))
            AiNodeSetFlt(node, str::intensity, 0.f);

        _ReadLightLinks(prim, node, context);
        ReadLightShaders(prim, prim.GetAttribute(_tokens->PrimvarsArnoldShaders), node, context);
    }
    return res;
}
