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

#include <pxr/base/gf/camera.h>
#include <pxr/usd/usdGeom/camera.h>

#include "read_camera.h"
#include "registry.h"
#include "utils.h"
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

void UsdArnoldReadCamera::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    UsdGeomCamera cam(prim);

    TfToken projection;
    cam.GetProjectionAttr().Get(&projection);

    bool persp = false;
    std::string camType;
    if (projection == UsdGeomTokens->perspective) {
        persp = true;
        camType = "persp_camera";
    } else if (projection == UsdGeomTokens->orthographic) {
        camType = "ortho_camera";
    } else {
        return;
    }

    AtNode *node = context.CreateArnoldNode(camType.c_str(), prim.GetPath().GetText());
    ExportMatrix(prim, node, time, context);

    if (persp) {
        // GfCamera has the utility functions to get the field of view,
        // so we don't need to duplicate the code here
        GfCamera gfCamera = cam.GetCamera(time.frame);
        float fov = gfCamera.GetFieldOfView(GfCamera::FOVHorizontal);
        AiNodeSetFlt(node, "fov", fov);

        VtValue focusDistanceValue;
        if (cam.CreateFocusDistanceAttr().Get(&focusDistanceValue)) {
            AiNodeSetFlt(node, "focus_distance", VtValueGetFloat(focusDistanceValue));
        }
    }
    GfVec2f clippingRange;
    cam.CreateClippingRangeAttr().Get(&clippingRange);
    AiNodeSetFlt(node, "near_clip", clippingRange[0]);
    AiNodeSetFlt(node, "far_clip", clippingRange[1]);

    VtValue shutterOpenValue;
    if (cam.GetShutterOpenAttr().Get(&shutterOpenValue)) {
        AiNodeSetFlt(node, "shutter_start", VtValueGetFloat(shutterOpenValue));
    }

    VtValue shutterCloseValue;
    if (cam.GetShutterCloseAttr().Get(&shutterCloseValue)) {
        AiNodeSetFlt(node, "shutter_end", VtValueGetFloat(shutterCloseValue));
    }

    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");
    ExportPrimvars(prim, node, time, context);
}
