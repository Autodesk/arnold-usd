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

static void exportLightCommon(const UsdLuxLight &light, AtNode *node)
{
    // This function is computing intensity, color, and eventually color
    // temperature. Another solution could be to export separately these
    // parameters, but it seems simpler to do this for now
    GfVec3f color = light.ComputeBaseEmission();
    AiNodeSetRGB(node, "color", color[0], color[1], color[2]);
    AiNodeSetFlt(node, "intensity", 1.f);
    AiNodeSetFlt(node, "exposure", 0.f);

    VtValue diffuse_attr;
    if (light.GetDiffuseAttr().Get(&diffuse_attr)) {
        AiNodeSetFlt(node, "diffuse", diffuse_attr.Get<float>());
    }
    VtValue specular_attr;
    if (light.GetSpecularAttr().Get(&specular_attr)) {
        AiNodeSetFlt(node, "specular", specular_attr.Get<float>());
    }

    /*
    This is preventing distant lights from working properly, so we should only
    do it where it makes sense VtValue normalize_attr;
    if(light.GetNormalizeAttr().Get(&normalize_attr))
       AiNodeSetBool(node, "normalize", normalize_attr.Get<bool>());
    */
}

void UsdArnoldReadDistantLight::read(const UsdPrim &prim, UsdArnoldReader &reader, bool create, bool convert)
{
    AtNode *node = getNodeToConvert(reader, "distant_light", prim.GetPath().GetText(), create, convert);
    if (node == nullptr) {
        return;
    }

    UsdLuxDistantLight light(prim);

    float angle = 0.52f;
    VtValue angle_attr;
    if (light.GetAngleAttr().Get(&angle_attr)) {
        AiNodeSetFlt(node, "angle", angle_attr.Get<float>());
    }

    const TimeSettings &time = reader.getTimeSettings();

    exportLightCommon(light, node);
    exportMatrix(prim, node, time, reader);
    readArnoldParameters(prim, reader, node, time);
}

void UsdArnoldReadDomeLight::read(const UsdPrim &prim, UsdArnoldReader &reader, bool create, bool convert)
{
    AtNode *node = getNodeToConvert(reader, "skydome_light", prim.GetPath().GetText(), create, convert);
    if (node == nullptr) {
        return;
    }

    UsdLuxDomeLight light(prim);

    // TODO : portal
    exportLightCommon(light, node);
    const TimeSettings &time = reader.getTimeSettings();

    SdfAssetPath texture_path;
    if (light.GetTextureFileAttr().Get(&texture_path)) {
        VtValue filename_vt(texture_path.GetResolvedPath());
        std::string filename = filename_vt.Get<std::string>();
        if (!filename.empty()) {
            // there's a texture filename, so we need to connect it to the color
            std::string imageName(prim.GetPath().GetText());
            imageName += "/texture_file";
            AtNode *image = reader.createArnoldNode("image", imageName.c_str());

            AiNodeSetStr(image, "filename", filename.c_str());
            AiNodeLink(image, "color", node);

            // now we need to export the intensity and exposure manually,
            // because we have overridden the color
            float intensity = 1.f;
            light.GetIntensityAttr().Get(&intensity);
            AiNodeSetFlt(node, "intensity", intensity);
            float exposure = 0.f;
            light.GetExposureAttr().Get(&exposure);
            AiNodeSetFlt(node, "exposure", exposure);
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
    AiNodeSetFlt(node, "camera", 0.f);

    exportMatrix(prim, node, time, reader);
    readArnoldParameters(prim, reader, node, time);
}

void UsdArnoldReadDiskLight::read(const UsdPrim &prim, UsdArnoldReader &reader, bool create, bool convert)
{
    AtNode *node = getNodeToConvert(reader, "disk_light", prim.GetPath().GetText(), create, convert);
    if (node == nullptr) {
        return;
    }

    UsdLuxDiskLight light(prim);

    const TimeSettings &time = reader.getTimeSettings();

    exportLightCommon(light, node);

    VtValue radius_attr;
    if (light.GetRadiusAttr().Get(&radius_attr)) {
        AiNodeSetFlt(node, "radius", radius_attr.Get<float>());
    }

    VtValue normalize_attr;
    if (light.GetNormalizeAttr().Get(&normalize_attr)) {
        AiNodeSetBool(node, "normalize", normalize_attr.Get<bool>());
    }

    exportMatrix(prim, node, time, reader);
    readArnoldParameters(prim, reader, node, time);
}

// Sphere lights get exported to arnold as a point light with a radius
void UsdArnoldReadSphereLight::read(const UsdPrim &prim, UsdArnoldReader &reader, bool create, bool convert)
{
    AtNode *node = getNodeToConvert(reader, "point_light", prim.GetPath().GetText(), create, convert);
    if (node == nullptr) {
        return;
    }

    UsdLuxSphereLight light(prim);
    exportLightCommon(light, node);

    bool treatAsPoint = false;
    if (light.GetTreatAsPointAttr().Get(&treatAsPoint) && (!treatAsPoint)) {
        VtValue radiusAttr;
        if (light.GetRadiusAttr().Get(&radiusAttr)) {
            AiNodeSetFlt(node, "radius", radiusAttr.Get<float>());
        }

        VtValue normalizeAttr;
        if (light.GetNormalizeAttr().Get(&normalizeAttr)) {
            AiNodeSetBool(node, "normalize", normalizeAttr.Get<bool>());
        }
    }

    const TimeSettings &time = reader.getTimeSettings();

    exportMatrix(prim, node, time, reader);
    readArnoldParameters(prim, reader, node, time);
}

void UsdArnoldReadRectLight::read(const UsdPrim &prim, UsdArnoldReader &reader, bool create, bool convert)
{
    AtNode *node = getNodeToConvert(reader, "quad_light", prim.GetPath().GetText(), create, convert);
    if (node == nullptr) {
        return;
    }

    const TimeSettings &time = reader.getTimeSettings();

    UsdLuxRectLight light(prim);
    exportLightCommon(light, node);

    float width = 1.f;
    float height = 1.f;

    light.GetWidthAttr().Get(&width);
    light.GetHeightAttr().Get(&height);

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
            AtNode *image = reader.createArnoldNode("image", image_name.c_str());

            AiNodeSetStr(image, "filename", filename.c_str());
            AiNodeLink(image, "color", node);

            // now we need to export the intensity and exposure manually,
            // because we have overridden the color
            float intensity = 1.f;
            light.GetIntensityAttr().Get(&intensity);
            AiNodeSetFlt(node, "intensity", intensity);
            float exposure = 0.f;
            light.GetExposureAttr().Get(&exposure);
            AiNodeSetFlt(node, "exposure", exposure);
        }
    }

    VtValue normalizeAttr;
    if (light.GetNormalizeAttr().Get(&normalizeAttr)) {
        AiNodeSetBool(node, "normalize", normalizeAttr.Get<bool>());
    }

    exportMatrix(prim, node, time, reader);
    readArnoldParameters(prim, reader, node, time);
}

void UsdArnoldReadGeometryLight::read(const UsdPrim &prim, UsdArnoldReader &reader, bool create, bool convert)
{
    // First check if the target geometry is indeed a mesh, otherwise this won't
    // work
    UsdLuxGeometryLight light(prim);

    const TimeSettings &time = reader.getTimeSettings();

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
        UsdPrim targetPrim = reader.getStage()->GetPrimAtPath(geomPath);
        if (!targetPrim.IsA<UsdGeomMesh>()) {
            continue; // arnold's mesh lights only support meshes
        }

        AtNode *node = nullptr;
        if (create) {
            std::string lightName = prim.GetPath().GetText();
            if (i > 0) {
                lightName += std::string("_") + std::string(targetPrim.GetPath().GetText());
            }
            node = reader.createArnoldNode("mesh_light", lightName.c_str());
        } else {
            node = AiNodeLookUpByName(targetPrim.GetPath().GetText(), reader.getProceduralParent());
        }
        if (node == nullptr || !convert) {
            continue;
        }

        // ensure the target mesh is exported.
        // FIXME : find a better way to avoid exporting the same data twice
        reader.readPrimitive(targetPrim, true, false);

        exportLightCommon(light, node);

        VtValue normalizeAttr;
        if (light.GetNormalizeAttr().Get(&normalizeAttr)) {
            AiNodeSetBool(node, "normalize", normalizeAttr.Get<bool>());
        }

        exportMatrix(prim, node, time, reader);
        readArnoldParameters(prim, reader, node, time);
    }
}
