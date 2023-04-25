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

#include "render_delegate.h"

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

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
    /// @param renderPaaram Pointer to a HdArnoldRenderParam instance.
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

protected:
    AtNode* _camera = nullptr; ///< Arnold camera node.
    HdArnoldRenderDelegate *_delegate = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE
