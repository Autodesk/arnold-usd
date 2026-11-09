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
/// @file camera.h
///
/// Utilities for translating Hydra Cameras for the Render Delegate.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>

#include "render_delegate.h"

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

using CameraParamMap = std::vector<std::tuple<TfToken, AtString>>;

class HdArnoldCamera : public HdCamera {
public:
    /// Constructor for HdArnoldCamera.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the material.
    HDARNOLD_API
    HdArnoldCamera(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

    /// Destructor for HdArnoldCamera.
    ///
    /// Destory all Arnold Shader Nodes created.
    ~HdArnoldCamera() override;

    /// Syncs the Hydra Camera to the Arnold Perspective/Orthographic Camera.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderParam Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    HDARNOLD_API
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;

    /// Returns the minimal set of dirty bits to place in the
    /// change tracker for use in the first sync of this prim.
    ///
    /// @return Initial dirty bits.
    HDARNOLD_API
    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Returns the Arnold camera node.
    ///
    /// @return Pointer to the Arnold camera node, can be nullptr.
    AtNode* GetCamera() const { return _camera; }

    /// @brief Returns the arnold screen window from the orthographic projection matrix
    /// @param projMatrix an orthographic projection matrix
    /// @return {scr_min_x, scr_min_y, scr_max_x, scr_max_y}
    static GfVec4f GetScreenWindowFromOrthoProjection(const GfMatrix4d &projMatrix);

    /// @brief Conforms the screen window of a perspective camera to the host's display window
    ///
    /// Arnold always matches the horizontal aperture across xres. When the host sends a CameraUtilFraming, Hydra
    /// conforms the camera window to the display window aspect with the render pass window policy instead
    /// (CameraUtilFraming::ApplyToProjectionMatrix), this does the same through the screen window (#2819).
    /// The state is kept, so the following syncs of the camera keep the conformed window.
    /// @param enabled False goes back to the camera's own screen window
    /// @param windowPolicy The render pass window policy
    /// @param displayAspect The display window aspect ratio, pixel aspect ratio included
    /// @param renderParam Used to interrupt the render when the screen window changes
    HDARNOLD_API
    void SetHostConform(
        bool enabled, CameraUtilConformWindowPolicy windowPolicy, double displayAspect,
        HdArnoldRenderParam* renderParam);

protected:
    /// Set the Arnold camera node
    ///
    ///
    void SetCamera(AtNode *newCamera);

    /// @brief Update the perspective camera parameters
    /// @param sceneDelegate 
    /// @param renderParam 
    /// @param dirtyBits 
    void UpdatePerspectiveParams(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits);

    /// @brief Update generic camera parameters
    /// @param sceneDelegate 
    /// @param renderParam 
    /// @param dirtyBits 
    void UpdateGenericParams(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits);

    /// @brief Link the shaders that can be connected to any camera through an
    ///        ArnoldNodeGraph primitive (filtermap, uv_remap, and the uv_camera
    ///        ray_origin / ray_direction)
    /// @param sceneDelegate
    /// @param renderParam
    /// @param dirtyBits
    void UpdateCameraShaders(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits);

    void SetClippingPlanes(HdSceneDelegate* sceneDelegate);
    
    void SetCameraParams(HdSceneDelegate* sceneDelegate, const CameraParamMap &cameraParams);


    AtNode * ReadCameraShader(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, const TfToken &param, const TfToken &terminal, HdDirtyBits* dirtyBits);

    /// @brief Writes the screen window of a perspective camera, conformed to the host if SetHostConform enabled it
    void WriteScreenWindow();

    AtNode* _camera = nullptr; ///< Arnold camera node.
    HdArnoldRenderDelegate *_delegate = nullptr;

    GfMatrix4d _projMatrix{1.0};             ///< Camera projection, not framed by the host.
    GfVec2f _screenWindowMin{-1.f, -1.f};    ///< Screen window authored with the arnold primvars, or the default.
    GfVec2f _screenWindowMax{1.f, 1.f};
    GfVec2f _apertureOffsetWindow{0.f, 0.f}; ///< The aperture offset in screen window units.
    bool _writeScreenWindow = true;          ///< False when the authored screen window has an unsupported type.
    bool _hostConform = false;               ///< See SetHostConform.
    CameraUtilConformWindowPolicy _hostWindowPolicy = CameraUtilFit;
    double _hostDisplayAspect = 1.0;
};

PXR_NAMESPACE_CLOSE_SCOPE
