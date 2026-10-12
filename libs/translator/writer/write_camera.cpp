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
#include "write_camera.h"
#include <constant_strings.h>

#include <ai.h>

#include <pxr/base/gf/camera.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/usdGeom/camera.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "registry.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

// Cameras reference their shaders (uv_remap on generic cameras, ray_origin /
// ray_direction on the uv_camera) through an ArnoldNodeGraph primitive, the same
// indirection lights and render settings use. The node graph is created lazily
// and shared across the camera's shader parameters.
UsdPrim _GetCameraNodeGraph(UsdPrim& prim, UsdArnoldWriter& writer)
{
    std::string nodeGraphName = prim.GetPath().GetString() + "/camera_shaders";
    SdfPath nodeGraphPath(nodeGraphName);
    UsdStageRefPtr stage = writer.GetUsdStage();
    UsdPrim nodeGraphPrim = stage->GetPrimAtPath(nodeGraphPath);
    if (!nodeGraphPrim) {
        nodeGraphPrim = stage->DefinePrim(nodeGraphPath, str::t_ArnoldNodeGraph);
        // Author the local path under primvars:arnold:name so the prim can still
        // be resolved when this file is referenced and the runtime path gets
        // remapped under a different namespace (see the node-graph name map).
        nodeGraphPrim.CreateAttribute(str::t_primvars_arnold_name, SdfValueTypeNames->String, false)
            .Set(nodeGraphName);
    }
    return nodeGraphPrim;
}

// If the camera parameter paramName is linked to a shader, author an
// ArnoldNodeGraph output terminal named after the parameter, connected to that
// shader, and reference it from the camera attribute both as a USD connection
// (preferred, resolved by the translator) and as a string path (fallback,
// resolved by the Hydra render delegate which can't follow the connection).
// Returns true if a link was written (in which case the caller must flag
// paramName as exported so the generic parameter writer doesn't author it again
// as a direct shader link).
bool _WriteCameraShaderLink(const AtNode* node, const char* paramName,
    UsdPrim& prim, UsdArnoldWriter& writer)
{
    const AtString paramNameStr(paramName);
    // ray_origin / ray_direction only exist on the uv_camera, so skip cameras
    // that don't have this parameter.
    if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), paramNameStr) == nullptr)
        return false;
    if (!AiNodeIsLinked(node, paramNameStr))
        return false;
    // Resolve the linked shader. Component-level links (attr.x, attr.r, …) return
    // a null node here; those aren't supported through the node graph, so we let
    // the generic parameter writer handle them.
    int outComp = -1;
    int outParam = -1;
    AtNode* target = AiNodeGetLinkOutput(node, paramNameStr, outParam, outComp);
    if (target == nullptr)
        return false;

    UsdStageRefPtr stage = writer.GetUsdStage();
    UsdPrim nodeGraphPrim = _GetCameraNodeGraph(prim, writer);
    const std::string nodeGraphName = nodeGraphPrim.GetPath().GetString();

    // Author the target shader and make sure it exposes an output to connect to.
    writer.WritePrimitive(target);
    const std::string targetName = UsdArnoldPrimWriter::GetArnoldNodeName(target, writer);
    UsdPrim targetPrim = stage->GetPrimAtPath(SdfPath(targetName));
    if (!targetPrim)
        return false;
    targetPrim.CreateAttribute(str::t_outputs_out, SdfValueTypeNames->Token, false);

    // Create the node graph output terminal named after the arnold parameter and
    // connect it to the shader. The terminal only carries a connection (like the
    // light / render-settings node graphs), so it stays a Token.
    const std::string terminalName = std::string("outputs:") + paramName;
    UsdAttribute terminalAttr = nodeGraphPrim.CreateAttribute(TfToken(terminalName),
        SdfValueTypeNames->Token, false);
    terminalAttr.AddConnection(SdfPath(targetName + std::string(".outputs:out")));

    // Reference the node graph terminal from the camera attribute.
    const std::string usdAttrName = std::string("primvars:arnold:") + paramName;
    UsdAttribute camAttr = prim.CreateAttribute(TfToken(usdAttrName), SdfValueTypeNames->String, false);
    camAttr.Set(nodeGraphName);
    camAttr.AddConnection(SdfPath(nodeGraphName + std::string(".") + terminalName));
    return true;
}

} // namespace

void UsdArnoldWriteCamera::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);
    writer.CreateHierarchy(objPath);
    UsdGeomCamera cam = UsdGeomCamera::Define(stage, objPath);
    UsdPrim prim = cam.GetPrim();

    TfToken projection;
    bool persp = false;
    if (_type == CAMERA_PERSPECTIVE) {
        persp = true;
        projection = TfToken("perspective");
    } else if (_type == CAMERA_ORTHOGRAPHIC) {
        projection = TfToken("orthographic");
    } else {
        UsdAttribute cameraTypeAttr = prim.CreateAttribute(str::t_primvars_arnold_camera, SdfValueTypeNames->Token, false);
        TfToken cameraType(AiNodeEntryGetName(AiNodeGetNodeEntry(node)));
        writer.SetAttribute(cameraTypeAttr, VtValue(cameraType));
    }
    if (!projection.IsEmpty())
        writer.SetAttribute(cam.CreateProjectionAttr(), projection);
    
    if (persp) {
        AtNode *options = AiUniverseGetOptions(writer.GetUniverse());
        // Arnold only knows about FOV and not about horizontal & vertical aperture.
        // We want to dump some aperture values in the usd file, as this is used by other
        // usd tools, but we can't compute an exact value (we'd need a focal length)
        // we only author these values if they weren't previously set. MTOA-1951
        if (!cam.GetHorizontalApertureAttr().HasAuthoredValue() && 
                !cam.GetVerticalApertureAttr().HasAuthoredValue()) {
            
            float fov = AiNodeGetFlt(node, AtString("fov"));
            float horizontalAperature = tan(fov * AI_DTOR * 0.5f);
            // The formula below assumes a focal length of 50, which is the default value but 
            // not necessarily the right one. Therefore we can't author an exact value here.
            horizontalAperature *= (2.f * 50.f * GfCamera::FOCAL_LENGTH_UNIT);
            horizontalAperature /= GfCamera::APERTURE_UNIT;
            writer.SetAttribute(cam.CreateHorizontalApertureAttr(), horizontalAperature);

            float verticalAperture = horizontalAperature;
            // Use the options image resolution to determine the vertical aperture
            if (options) {
                verticalAperture *= (float)AiNodeGetInt(options, AtString("yres")) / (float)AiNodeGetInt(options, AtString("xres"));
            }
            writer.SetAttribute(cam.CreateVerticalApertureAttr(), verticalAperture);
        }
        // Note that we're not adding "fov" to the list of exported attrs, because we still
        // want it to be set as an arnold-specific attribute. This way, when it's read from usd,
        // we can get the exact same value without any difference caused by the back and forth conversions
        writer.SetAttribute(cam.CreateFocusDistanceAttr(), AiNodeGetFlt(node, AtString("focus_distance")));
        _exportedAttrs.insert("focus_distance");
    }

    // To be written in both perspective and orthographic cameras:
    GfVec2f clippingRange = GfVec2f(AiNodeGetFlt(node, AtString("near_clip")), AiNodeGetFlt(node, AtString("far_clip")));
    writer.SetAttribute(cam.CreateClippingRangeAttr(), clippingRange);

    _exportedAttrs.insert("near_clip");
    _exportedAttrs.insert("far_clip");

    writer.SetAttribute(cam.CreateShutterOpenAttr(), double(AiNodeGetFlt(node, AtString("shutter_start"))));
    writer.SetAttribute(cam.CreateShutterCloseAttr(), double(AiNodeGetFlt(node, AtString("shutter_end"))));

    _exportedAttrs.insert("shutter_start");
    _exportedAttrs.insert("shutter_end");

    _WriteMatrix(cam, node, writer);

    AtArray* matrices = AiNodeGetArray(node, AtString("matrix"));
    unsigned int numKeys = matrices ? AiArrayGetNumKeys(matrices) : 0;
    bool hasMatrix = false;
    for (unsigned int i = 0; i < numKeys; ++i) {
        if (!AiM4IsIdentity(AiArrayGetMtx(matrices, i))) {
            hasMatrix = true;
            break;
        }
    }
    if (hasMatrix) {
        _exportedAttrs.insert("look_at");
        _exportedAttrs.insert("up");
        _exportedAttrs.insert("position");
    }

    // Camera shaders linked through an ArnoldNodeGraph (uv_remap, and the
    // uv_camera ray_origin / ray_direction). These must be authored before
    // _WriteArnoldParameters, and flagged as exported so the generic writer
    // doesn't author them again as a direct shader link.
    for (const char* shaderParam : {"uv_remap", "ray_origin", "ray_direction"}) {
        if (_WriteCameraShaderLink(node, shaderParam, prim, writer))
            _exportedAttrs.insert(shaderParam);
    }

    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
}
