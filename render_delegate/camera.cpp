// Copyright 2020 Autodesk, Inc.
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
#include "camera.h"

#include <pxr/base/gf/range1f.h>

#include <constant_strings.h>
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
 (exposure)
);
// clang-format on

HdArnoldCamera::HdArnoldCamera(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id) : HdCamera(id)
{
    // We create a persp_camera by default and optionally replace the node in ::Sync.
    _camera = AiNode(renderDelegate->GetUniverse(), str::persp_camera);
    if (!id.IsEmpty()) {
        AiNodeSetStr(_camera, str::name, AtString(id.GetText()));
    }
}

HdArnoldCamera::~HdArnoldCamera() {
    if (_camera) {
        // Check if this camera node is referenced in the options
        // and clear the attributes if needed
        AtNode *options = AiUniverseGetOptions(AiNodeGetUniverse(_camera));
        if (_camera == AiNodeGetPtr(options, str::camera))
            AiNodeResetParameter(options, str::camera);

        if (_camera == AiNodeGetPtr(options, str::subdiv_dicing_camera))
            AiNodeResetParameter(options, str::subdiv_dicing_camera);        

        AiNodeDestroy(_camera); 
    }
}

void HdArnoldCamera::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    auto oldBits = *dirtyBits;
    HdCamera::Sync(sceneDelegate, renderParam, &oldBits);

    // We can change between perspective and orthographic camera.
    if (*dirtyBits & HdCamera::DirtyProjMatrix) {
        param->Interrupt();
        // If 3, 3 is zero then it's a perspective matrix.
        // TODO(pal): Add support for orthographic cameras.
        const auto& projMatrix = GetProjectionMatrix();
        const auto fov = static_cast<float>(GfRadiansToDegrees(atan(1.0 / projMatrix[0][0]) * 2.0));
        AiNodeSetFlt(_camera, str::fov, fov);
    }

    if (*dirtyBits & HdCamera::DirtyViewMatrix) {
        param->Interrupt();
        HdArnoldSetTransform(_camera, sceneDelegate, GetId());
    }

    // TODO(pal): Investigate how horizontalAperture, verticalAperture, horizontalApertureOffset and
    //  verticalApertureOffset should be used.
    if (*dirtyBits & HdCamera::DirtyParams) {
        param->Interrupt();
        const auto& id = GetId();
        const auto getFloat = [&](const VtValue& value, float defaultValue) -> float {
            if (value.IsHolding<float>()) {
                return value.UncheckedGet<float>();
            } else if (value.IsHolding<double>()) {
                return static_cast<float>(value.UncheckedGet<double>());
            } else {
                return defaultValue;
            }
        };
        const auto focalLength = getFloat(sceneDelegate->GetCameraParamValue(id, HdCameraTokens->focalLength), 0.0f);
        const auto fStop = getFloat(sceneDelegate->GetCameraParamValue(id, HdCameraTokens->fStop), 0.0f);
        if (GfIsClose(fStop, 0.0f, AI_EPSILON)) {
            AiNodeSetFlt(_camera, str::aperture_size, 0.0f);
        } else {
            AiNodeSetFlt(_camera, str::aperture_size, focalLength / (2.0f * fStop));
            AiNodeSetFlt(
                _camera, str::focus_distance,
                getFloat(sceneDelegate->GetCameraParamValue(id, HdCameraTokens->focusDistance), 0.0f));
        }
        const auto clippingRange = sceneDelegate->GetCameraParamValue(id, HdCameraTokens->clippingRange);
        if (clippingRange.IsHolding<GfRange1f>()) {
            const auto& range = clippingRange.UncheckedGet<GfRange1f>();
            AiNodeSetFlt(_camera, str::near_clip, range.GetMin());
            AiNodeSetFlt(_camera, str::far_clip, range.GetMax());
        } else {
            AiNodeSetFlt(_camera, str::near_clip, 0.0f);
            AiNodeSetFlt(_camera, str::far_clip, AI_INFINITE);
        }
        using CameraParamMap = std::vector<std::tuple<TfToken, AtString>>;
        const static CameraParamMap cameraParams = []() -> CameraParamMap {
            // Exposure seems to be part of the UsdGeom schema but not exposed on the Solaris camera lop. We look for
            // both the primvar and the built-in attribute, and preferring the primvar over the built-in attribute.
            CameraParamMap ret;
            ret.emplace_back(_tokens->exposure, str::exposure);
            ret.emplace_back(HdCameraTokens->shutterOpen, str::shutter_start);
            ret.emplace_back(HdCameraTokens->shutterClose, str::shutter_end);
            for (const auto* paramName :
                 {"exposure", "radial_distortion", "radial_distortion_type", "shutter_type", "rolling_shutter",
                  "rolling_shutter_duration", "aperture_blades", "aperture_rotation", "aperture_blade_curvature",
                  "aperture_aspect_ratio", "flat_field_focus", "lens_tilt_angle", "lens_shift"}) {
                ret.emplace_back(TfToken(TfStringPrintf("primvars:arnold:%s", paramName)), AtString(paramName));
            }
            return ret;
        }();
        const auto* nodeEntry = AiNodeGetNodeEntry(_camera);
        for (const auto& paramDesc : cameraParams) {
            const auto paramValue = sceneDelegate->GetCameraParamValue(id, std::get<0>(paramDesc));
            if (paramValue.IsEmpty()) {
                continue;
            }
            const auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, std::get<1>(paramDesc));
            if (Ai_likely(paramEntry != nullptr)) {
                HdArnoldSetParameter(_camera, paramEntry, paramValue);
            }
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldCamera::GetInitialDirtyBitsMask() const
{
    // HdCamera does not ask for DirtyParams.
    return HdCamera::GetInitialDirtyBitsMask() | HdCamera::DirtyParams;
}

PXR_NAMESPACE_CLOSE_SCOPE
