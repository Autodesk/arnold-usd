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

#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

void exportLightCommon(const UsdLuxLight &light, AtNode *node)
{
    // This function is computing intensity, color, and eventually color
    // temperature. Another solution could be to export separately these
    // parameters, but it seems simpler to do this for now
    
    VtValue colorAttr;
    GfVec3f color(1.f, 1.f, 1.f);
    if (light.GetColorAttr().Get(&colorAttr)) 
        color = colorAttr.Get<GfVec3f>();
    
    VtValue intensityAttr;
    float intensity = 1.f;
    if (light.GetIntensityAttr().Get(&intensityAttr)) {
        intensity = vtValueGetFloat(intensityAttr);
        AiNodeSetFlt(node, "intensity", intensity);
    }

    VtValue exposureAttr;
    float exposure = 0.f;
    if (light.GetExposureAttr().Get(&exposureAttr)) {
        exposure = vtValueGetFloat(exposureAttr);
        AiNodeSetFlt(node, "exposure", exposure);
    }

    VtValue enableTemperatureAttr;
    if (light.GetEnableColorTemperatureAttr().Get(&enableTemperatureAttr)) {
        if (vtValueGetBool(enableTemperatureAttr)) {
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
    AiNodeSetRGB(node, "color", color[0], color[1], color[2]);

    VtValue diffuse_attr;
    if (light.GetDiffuseAttr().Get(&diffuse_attr)) {
        AiNodeSetFlt(node, "diffuse", vtValueGetFloat(diffuse_attr));
    }
    VtValue specular_attr;
    if (light.GetSpecularAttr().Get(&specular_attr)) {
        AiNodeSetFlt(node, "specular", vtValueGetFloat(specular_attr));
    }

    /*
    This is preventing distant lights from working properly, so we should only
    do it where it makes sense VtValue normalize_attr;
    if(light.GetNormalizeAttr().Get(&normalize_attr))
       AiNodeSetBool(node, "normalize", normalize_attr.Get<bool>());
    */
}

// Check if some shader is linked to the light color (for skydome and quad lights only in arnold)
void exportLightColorLinks(const UsdLuxLight &light, AtNode* node, UsdArnoldReaderContext &context)
{
    UsdAttribute lightColor = light.GetColorAttr();
    if (lightColor.HasAuthoredConnections()) {
        SdfPathVector connections;
        if (lightColor.GetConnections(&connections) && !connections.empty()) {
            // note that arnold only supports a single connection
            context.addConnection(
                node, "color", connections[0].GetPrimPath().GetText(), UsdArnoldReaderContext::CONNECTION_LINK);
        }
    }
}

}

void UsdArnoldReadDistantLight::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("distant_light", prim.GetPath().GetText());
    UsdLuxDistantLight light(prim);

    float angle = 0.52f;
    VtValue angle_attr;
    if (light.GetAngleAttr().Get(&angle_attr)) {
        AiNodeSetFlt(node, "angle", vtValueGetFloat(angle_attr));
    }

    const TimeSettings &time = context.getTimeSettings();

    exportLightCommon(light, node);
    exportMatrix(prim, node, time, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");
    
    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.getPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, "intensity", 0.f);
}

void UsdArnoldReadDomeLight::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("skydome_light", prim.GetPath().GetText());
    
    UsdLuxDomeLight light(prim);

    // TODO : portal
    exportLightCommon(light, node);
    const TimeSettings &time = context.getTimeSettings();

    SdfAssetPath texture_path;
    if (light.GetTextureFileAttr().Get(&texture_path)) {
        VtValue filename_vt(texture_path.GetResolvedPath());
        std::string filename = filename_vt.Get<std::string>();
        if (!filename.empty()) {
            // there's a texture filename, so we need to connect it to the color
            std::string imageName(prim.GetPath().GetText());
            imageName += "/texture_file";
            AtNode *image = context.createArnoldNode("image", imageName.c_str());

            AiNodeSetStr(image, "filename", filename.c_str());
            AiNodeLink(image, "color", node);

            // now we need to export the intensity and exposure manually,
            // because we have overridden the color
            
            VtValue intensity_attr;
            if (light.GetIntensityAttr().Get(&intensity_attr))
                AiNodeSetFlt(node, "intensity", vtValueGetFloat(intensity_attr));
            VtValue exposure_attr;
            if (light.GetExposureAttr().Get(&exposure_attr))
                AiNodeSetFlt(node, "exposure", vtValueGetFloat(exposure_attr));
        }
    }
    TfToken format;
    if (light.GetTextureFormatAttr().Get(&format)) {
        if (format == UsdLuxTokens->latlong) {
            AiNodeSetStr(node, "format", AtString("latlong"));
        } else if (format == UsdLuxTokens->mirroredBall) {
            AiNodeSetStr(node, "format", AtString("mirrored_ball"));
        } else if (format == UsdLuxTokens->angular) {
            AiNodeSetStr(node, "format", AtString("angular"));
        }
    }

    // Special case, the attribute "color" can be linked to some shader
    exportLightColorLinks(light, node, context);

    exportMatrix(prim, node, time, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.getPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, "intensity", 0.f);
}

void UsdArnoldReadDiskLight::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("disk_light", prim.GetPath().GetText());
    
    UsdLuxDiskLight light(prim);

    const TimeSettings &time = context.getTimeSettings();

    exportLightCommon(light, node);

    VtValue radius_attr;
    if (light.GetRadiusAttr().Get(&radius_attr)) {
        AiNodeSetFlt(node, "radius", vtValueGetFloat(radius_attr));
    }

    VtValue normalize_attr;
    if (light.GetNormalizeAttr().Get(&normalize_attr)) {
        AiNodeSetBool(node, "normalize", vtValueGetBool(normalize_attr));
    }

    exportMatrix(prim, node, time, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.getPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, "intensity", 0.f);
}

// Sphere lights get exported to arnold as a point light with a radius
void UsdArnoldReadSphereLight::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("point_light", prim.GetPath().GetText());
    
    UsdLuxSphereLight light(prim);
    exportLightCommon(light, node);

    bool treatAsPoint = false;
    VtValue treatAsPointAttr;
    if (light.GetTreatAsPointAttr().Get(&treatAsPointAttr)) {
        treatAsPoint = vtValueGetBool(treatAsPointAttr);
        if (!treatAsPoint) {
            VtValue radiusAttr;
            if (light.GetRadiusAttr().Get(&radiusAttr)) {
                AiNodeSetFlt(node, "radius", vtValueGetFloat(radiusAttr));
            }

            VtValue normalizeAttr;
            if (light.GetNormalizeAttr().Get(&normalizeAttr)) {
                AiNodeSetBool(node, "normalize", vtValueGetBool(normalizeAttr));
            }
        }
    }

    const TimeSettings &time = context.getTimeSettings();

    exportMatrix(prim, node, time, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.getPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, "intensity", 0.f);
}

void UsdArnoldReadRectLight::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("quad_light", prim.GetPath().GetText());
    
    const TimeSettings &time = context.getTimeSettings();

    UsdLuxRectLight light(prim);
    exportLightCommon(light, node);

    float width = 1.f;
    float height = 1.f;
    VtValue widthAttr, heightAttr;

    if (light.GetWidthAttr().Get(&widthAttr))
        width = vtValueGetFloat(widthAttr);
    if (light.GetHeightAttr().Get(&heightAttr))
        height = vtValueGetFloat(heightAttr);

    width /= 2.f;
    height /= 2.f;

    AtVector vertices[4];
    vertices[3] = AtVector(width, height, 0);
    vertices[0] = AtVector(width, -height, 0);
    vertices[1] = AtVector(-width, -height, 0);
    vertices[2] = AtVector(-width, height, 0);
    AiNodeSetArray(node, "vertices", AiArrayConvert(4, 1, AI_TYPE_VECTOR, vertices));

    SdfAssetPath texturePath;
    if (light.GetTextureFileAttr().Get(&texturePath)) {
        VtValue filename_vt(texturePath.GetResolvedPath());
        std::string filename = filename_vt.Get<std::string>();
        if (!filename.empty()) {
            // there's a texture filename, so we need to connect it to the color
            std::string image_name(prim.GetPath().GetText());
            image_name += "/texture_file";
            AtNode *image = context.createArnoldNode("image", image_name.c_str());

            AiNodeSetStr(image, "filename", filename.c_str());
            AiNodeLink(image, "color", node);

            // now we need to export the intensity and exposure manually,
            // because we have overridden the color
            VtValue intensityAttr;
            if (light.GetIntensityAttr().Get(&intensityAttr))
                AiNodeSetFlt(node, "intensity", vtValueGetFloat(intensityAttr));
            VtValue exposureAttr;
            if (light.GetExposureAttr().Get(&exposureAttr))
                AiNodeSetFlt(node, "exposure", vtValueGetFloat(exposureAttr));
        }
    }
    // Special case, the attribute "color" can be linked to some shader
    exportLightColorLinks(light, node, context);

    VtValue normalizeAttr;
    if (light.GetNormalizeAttr().Get(&normalizeAttr)) {
        AiNodeSetBool(node, "normalize", vtValueGetBool(normalizeAttr));
    }

    exportMatrix(prim, node, time, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");
    
    // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
    if (!context.getPrimVisibility(prim, time.frame))
        AiNodeSetFlt(node, "intensity", 0.f);
}

void UsdArnoldReadGeometryLight::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    // First check if the target geometry is indeed a mesh, otherwise this won't
    // work
    UsdLuxGeometryLight light(prim);

    const TimeSettings &time = context.getTimeSettings();

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
        UsdPrim targetPrim = context.getReader()->getStage()->GetPrimAtPath(geomPath);
        if (!targetPrim.IsA<UsdGeomMesh>()) {
            continue; // arnold's mesh lights only support meshes
        }

        AtNode *node = nullptr;
        std::string lightName = prim.GetPath().GetText();
        if (i > 0) {
            lightName += std::string("_") + std::string(targetPrim.GetPath().GetText());
        }
        node = context.createArnoldNode("mesh_light", lightName.c_str());
        context.addConnection(node, "mesh", targetPrim.GetPath().GetText(), UsdArnoldReaderContext::CONNECTION_PTR);
        
        exportLightCommon(light, node);

        VtValue normalizeAttr;
        if (light.GetNormalizeAttr().Get(&normalizeAttr)) {
            AiNodeSetBool(node, "normalize", vtValueGetBool(normalizeAttr));
        }

        exportMatrix(prim, node, time, context);
        readArnoldParameters(prim, context, node, time, "primvars:arnold");

       // Check the primitive visibility, set the intensity to 0 if it's meant to be hidden
       if (!context.getPrimVisibility(prim, time.frame))
            AiNodeSetFlt(node, "intensity", 0.f);
    }
}
