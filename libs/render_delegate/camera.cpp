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
#include "camera.h"

#include <pxr/base/gf/range1f.h>
#include "node_graph.h"
#include <constant_strings.h>
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
 (exposure)
 ((filtermap, "primvars:arnold:filtermap"))
 ((uv_remap, "primvars:arnold:uv_remap"))

);
// clang-format on

HdArnoldCamera::HdArnoldCamera(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id) : HdCamera(id)
{
    // We create a persp_camera by default and optionally replace the node in ::Sync. as at this point we don't know if it's an ortho camera
    _camera = renderDelegate->CreateArnoldNode(str::persp_camera, AtString(id.GetText()));
    _delegate = renderDelegate;
}

HdArnoldCamera::~HdArnoldCamera() {
    if (_camera)
        _delegate->DestroyArnoldNode(_camera);
}

AtNode * HdArnoldCamera::ReadCameraShader(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, const TfToken &param, const TfToken &terminal, HdDirtyBits* dirtyBits) {
    const auto shaderValue = sceneDelegate->GetCameraParamValue(GetId(), param);
    const std::string shaderStr = shaderValue.IsHolding<std::string>() ? 
        shaderValue.Get<std::string>() : std::string();
    if (shaderStr.empty())
        return nullptr;

    SdfPath shaderPath(shaderStr.c_str());
    auto* shaderNodeGraph = HdArnoldNodeGraph::GetNodeGraph(sceneDelegate->GetRenderIndex(), shaderPath);
    HdArnoldRenderDelegate::PathSetWithDirtyBits pathSet;
    pathSet.insert({shaderPath, HdChangeTracker::DirtyMaterialId});
    _delegate->TrackDependencies(GetId(), pathSet);

    if (shaderNodeGraph) {
        return shaderNodeGraph->GetOrCreateTerminal(sceneDelegate, terminal);
    }
    return nullptr;
};


GfVec4f HdArnoldCamera::GetScreenWindowFromOrthoProjection(const GfMatrix4d &orthoProj) {
    if (orthoProj[0][0] == 0.0) {
        return {-1.f, 1.f, -1.f, 1.f};
    }
    const float unitX = 1.f / orthoProj[0][0];
    return { static_cast<float>(-unitX - orthoProj[3][0] * unitX), static_cast<float>(-unitX - orthoProj[3][1] * unitX), 
             static_cast<float>( unitX - orthoProj[3][0] * unitX), static_cast<float>( unitX - orthoProj[3][1] * unitX) };
}

void HdArnoldCamera::SetClippingPlanes(HdSceneDelegate* sceneDelegate) {
    const auto clippingRange = sceneDelegate->GetCameraParamValue(GetId(), HdCameraTokens->clippingRange);
    if (clippingRange.IsHolding<GfRange1f>()) {
        const auto& range = clippingRange.UncheckedGet<GfRange1f>();
        AiNodeSetFlt(_camera, str::near_clip, range.GetMin());
        AiNodeSetFlt(_camera, str::far_clip, range.GetMax());
    } else {
        AiNodeSetFlt(_camera, str::near_clip, 0.0f);
        AiNodeSetFlt(_camera, str::far_clip, AI_INFINITE);
    }
}

void HdArnoldCamera::SetCameraParams(HdSceneDelegate* sceneDelegate, const CameraParamMap &cameraParams) {
    const auto* nodeEntry = AiNodeGetNodeEntry(_camera);
    for (const auto& paramDesc : cameraParams) {
        const auto paramValue = sceneDelegate->GetCameraParamValue(GetId(), std::get<0>(paramDesc));
        if (paramValue.IsEmpty()) {
            continue;
        }
        const auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, std::get<1>(paramDesc));
        if (Ai_likely(paramEntry != nullptr)) {
            HdArnoldSetParameter(_camera, paramEntry, paramValue, _delegate);
        }
    }

    // Now iterate through all the camera's arnold attributes, and check if they're
    // defined in the camera primitive #1738
    AtParamIterator* paramIter = AiNodeEntryGetParamIterator(nodeEntry);
    while (!AiParamIteratorFinished(paramIter)) {

        const AtParamEntry* param = AiParamIteratorGetNext(paramIter);
        const AtString paramName = AiParamGetName(param);
        if (paramName == str::motion_start || paramName == str::motion_end)
            continue;
        
        TfToken attr(TfStringPrintf("primvars:arnold:%s", paramName.c_str()));
        const auto paramValue = sceneDelegate->GetCameraParamValue(GetId(), attr);
        if (!paramValue.IsEmpty()) {
            HdArnoldSetParameter(_camera, param, paramValue, _delegate);
        }
    }
    AiParamIteratorDestroy(paramIter);
}

void HdArnoldCamera::UpdateGenericParams(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) {
    
    // Set the clipping planes
    SetClippingPlanes(sceneDelegate);

    // Set bunch of parameters
    const static CameraParamMap cameraParams = []() -> CameraParamMap {
        // Exposure seems to be part of the UsdGeom schema but not exposed on the Solaris camera lop. We look for
        // both the primvar and the built-in attribute, and preferring the primvar over the built-in attribute.
        CameraParamMap ret;
        ret.emplace_back(_tokens->exposure, str::exposure);
        ret.emplace_back(HdCameraTokens->shutterOpen, str::shutter_start);
        ret.emplace_back(HdCameraTokens->shutterClose, str::shutter_end);
        return ret;
    }();

    SetCameraParams(sceneDelegate, cameraParams);

    // Set the filter map
    const AtNode *filtermap = ReadCameraShader(sceneDelegate, renderParam, _tokens->filtermap, str::t_filtermap, dirtyBits);
    if (filtermap)
        AiNodeSetPtr(_camera, str::filtermap, (void*)filtermap);
    else 
        AiNodeResetParameter(_camera, str::filtermap);
}

void HdArnoldCamera::UpdatePerspectiveParams(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) {
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
    const auto focalLength = getFloat(sceneDelegate->GetCameraParamValue(id, HdCameraTokens->focalLength), 50.0f);
    const auto fStop = getFloat(sceneDelegate->GetCameraParamValue(id, HdCameraTokens->fStop), 0.0f);
    if (GfIsClose(fStop, 0.0f, AI_EPSILON)) {
        AiNodeSetFlt(_camera, str::aperture_size, 0.0f);
    } else {
        AiNodeSetFlt(_camera, str::aperture_size, focalLength / (2.0f * fStop));
        AiNodeSetFlt(
            _camera, str::focus_distance,
            getFloat(sceneDelegate->GetCameraParamValue(id, HdCameraTokens->focusDistance), 0.0f));
    }
    SetClippingPlanes(sceneDelegate);

    const static CameraParamMap cameraParams = []() -> CameraParamMap {
        // Exposure seems to be part of the UsdGeom schema but not exposed on the Solaris camera lop. We look for
        // both the primvar and the built-in attribute, and preferring the primvar over the built-in attribute.
        CameraParamMap ret;
        ret.emplace_back(_tokens->exposure, str::exposure);
        ret.emplace_back(HdCameraTokens->shutterOpen, str::shutter_start);
        ret.emplace_back(HdCameraTokens->shutterClose, str::shutter_end);
        ret.emplace_back(HdCameraTokens->focusDistance, str::focus_distance);
        return ret;
    }();

    SetCameraParams(sceneDelegate, cameraParams);

    // TODO(pal): Investigate how horizontalAperture, verticalAperture, horizontalApertureOffset and
    //  verticalApertureOffset should be used.
    float horizontalApertureOffset = sceneDelegate->GetCameraParamValue(id, HdCameraTokens->horizontalApertureOffset).Get<float>();
    float verticalApertureOffset = sceneDelegate->GetCameraParamValue(id, HdCameraTokens->verticalApertureOffset).Get<float>();
    const float horizontalAperture = sceneDelegate->GetCameraParamValue(id, HdCameraTokens->horizontalAperture).Get<float>();
    const float verticalAperture = sceneDelegate->GetCameraParamValue(id, HdCameraTokens->verticalAperture).Get<float>();
    if (horizontalApertureOffset!=0.f || verticalApertureOffset!=0.f) {
        horizontalApertureOffset = 2.f*horizontalApertureOffset/horizontalAperture;
        verticalApertureOffset = 2.f*verticalApertureOffset/verticalAperture;
        AiNodeSetVec2(_camera, str::screen_window_min, -1+horizontalApertureOffset, -1+verticalApertureOffset);
        AiNodeSetVec2(_camera, str::screen_window_max, 1+horizontalApertureOffset, 1+verticalApertureOffset);
    }

    const AtNode *filtermap = ReadCameraShader(sceneDelegate, renderParam, _tokens->filtermap, str::t_filtermap, dirtyBits);
    if (filtermap)
        AiNodeSetPtr(_camera, str::filtermap, (void*)filtermap);
    else 
        AiNodeResetParameter(_camera, str::filtermap);
    AtNode *uv_remap = ReadCameraShader(sceneDelegate, renderParam, _tokens->uv_remap, str::t_uv_remap, dirtyBits);
    if (uv_remap)
        AiNodeLink(uv_remap, str::uv_remap, _camera);
    else 
        AiNodeResetParameter(_camera, str::uv_remap);
}

void HdArnoldCamera::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    if (!_delegate->CanUpdateScene())
        return;
 
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    auto oldBits = *dirtyBits;
    HdCamera::Sync(sceneDelegate, renderParam, &oldBits);

    const auto projection = GetProjection();
    AtString cameraType = 
         (projection == HdCamera::Projection::Orthographic) ? str::ortho_camera : str::persp_camera;

    VtValue cameraTypeVal = sceneDelegate->Get(GetId(), str::t_primvars_arnold_camera);
    if (!cameraTypeVal.IsEmpty()) {     
        std::string cameraTypeStr = VtValueGetString(cameraTypeVal);
        if (!cameraTypeStr.empty())
            cameraType = AtString(cameraTypeStr.c_str());
    }

    if (_camera == nullptr || !AiNodeIs(_camera, cameraType)) {
        // The camera type has changed, let's create a new node and delete the previous one
        param->Interrupt();
        
        // First reset the previous camera name so that we can create a new one with that same name
        if (_camera)
            AiNodeSetStr(_camera, str::name, AtString());

        AtNode *newCamera = _delegate->CreateArnoldNode(cameraType, AtString(GetId().GetText()));
        
        if (_camera) {
            // in theory AiNodeReplace should handle the node replacement, 
            // but in batch render the dependency graph is disabled and we 
            // might have already set the render camera
            AtNode *options = AiUniverseGetOptions(_delegate->GetUniverse());
            if (_camera == AiNodeGetPtr(options, str::camera))
                AiNodeSetPtr(options, str::camera, newCamera);
            if (_camera == AiNodeGetPtr(options, str::subdiv_dicing_camera))
                AiNodeSetPtr(options, str::subdiv_dicing_camera, newCamera);

            if (!_delegate->IsBatchContext())   
                AiNodeReplace(_camera, newCamera, false);
            _delegate->DestroyArnoldNode(_camera);
        }
        
        _camera = newCamera;
    }

    // We can change between perspective and orthographic camera.
#if PXR_VERSION >= 2203
    if (*dirtyBits & HdCamera::AllDirty) {
        param->Interrupt();
        const auto projMatrix = ComputeProjectionMatrix();
#else
    if (*dirtyBits & HdCamera::DirtyProjMatrix) {
        param->Interrupt();
        const auto& projMatrix = GetProjectionMatrix();
#endif
        if (cameraType == str::persp_camera) {
            // TODO cyril: pixel aspect ratio is incorrect here, we should set the matrix instead of the fov ?
            const auto fov = static_cast<float>(GfRadiansToDegrees(atan(1.0 / projMatrix[0][0]) * 2.0));
            AiNodeSetFlt(_camera, str::fov, fov);
        } else if (cameraType == str::ortho_camera) {
            GfVec4f screenWindow(GetScreenWindowFromOrthoProjection(projMatrix));
            AiNodeSetVec2(_camera, str::screen_window_min, screenWindow[0], screenWindow[1]);
            AiNodeSetVec2(_camera, str::screen_window_max, screenWindow[2], screenWindow[3]);
        }
    }

#if PXR_VERSION >= 2203
    if (*dirtyBits & HdCamera::AllDirty) {
#else
    if (*dirtyBits & HdCamera::DirtyViewMatrix) {
#endif
        param->Interrupt();
        HdArnoldRenderParam * renderParam = reinterpret_cast<HdArnoldRenderParam*>(_delegate->GetRenderParam());
        HdArnoldSetTransform(_camera, sceneDelegate, GetId(), renderParam->GetShutterRange());
        // In arnold, parent matrices are not applied properly to cameras.
        // We fake this by applying the parent procedural matrix here
        const AtNode *parent = _delegate->GetProceduralParent();
        if (parent) {
            AtArray *cameraMatrices = AiNodeGetArray(_camera, str::matrix);
            unsigned int cameraMatricesNumKeys = cameraMatrices  ? AiArrayGetNumKeys(cameraMatrices) : 0;
            if (cameraMatricesNumKeys > 0) {
                while (parent) {
                    const AtArray *parentMatrices = AiNodeGetArray(parent, str::matrix);
                    unsigned int parentMatrixNumKeys = parentMatrices && AiArrayGetNumElements(parentMatrices) > 0 ?
                        AiArrayGetNumKeys(parentMatrices) : 0;
                    if (parentMatrixNumKeys > 0) {
                        
                        for (int i = 0; i < cameraMatricesNumKeys; ++i) {
                            AtMatrix m = AiArrayGetMtx(cameraMatrices, i);
                            float t = (float)i / AiMax(float(cameraMatricesNumKeys - 1), 1.f);
                            m = AiM4Mult(m, AiArrayInterpolateMtx(parentMatrices, t, 0));
                            AiArraySetMtx(cameraMatrices, i, m);
                        }
                    }
                    parent = AiNodeGetParent(parent);
                }
                AiNodeSetArray(_camera, str::matrix, cameraMatrices);

            }            
        }
    }

    if (*dirtyBits & HdCamera::DirtyParams) {
        param->Interrupt();
        if (cameraType == str::persp_camera) {
            UpdatePerspectiveParams(sceneDelegate, renderParam, dirtyBits);
        } else {
            UpdateGenericParams(sceneDelegate, renderParam, dirtyBits);
        }
    }
    // The camera can be used as a projection camera in which case it needs to dirty its dependencies
    _delegate->DirtyDependency(GetId());

    // TODO: should we split the dirtyclipplanes from the params ??
    // if (*dirtyBits & HdCamera::DirtyClipPlanes) {}
    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldCamera::GetInitialDirtyBitsMask() const
{
    // HdCamera does not ask for DirtyParams.
    return HdCamera::GetInitialDirtyBitsMask() | HdCamera::DirtyParams;
}

PXR_NAMESPACE_CLOSE_SCOPE
