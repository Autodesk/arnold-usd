//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2026 Autodesk, Inc.
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
/// @file render_settings.h
///
/// Hydra 2.0 Render Settings Prim for Arnold.
#pragma once

#include "api.h"

#include <pxr/pxr.h>

#if PXR_VERSION >= 2308

#include <pxr/imaging/hd/renderSettings.h>

#include <ai.h>

#include "hdarnold.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRenderIndex;
class HdArnoldRenderParam;

/// Hydra 2.0 Render Settings Prim for Arnold.
///
/// This class represents a render settings prim in Hydra 2.0 for the Arnold
/// render delegate. It is responsible for:
/// - Syncing render settings from the scene
/// - Processing render products and render vars
/// - Configuring Arnold render options and outputs
/// - Driving batch rendering when appropriate
class HdArnoldRenderSettings final : public HdRenderSettings {
public:
    /// Constructor.
    ///
    /// @param id The scene delegate path for this render settings prim.
    HDARNOLD_API
    HdArnoldRenderSettings(SdfPath const& id);

    /// Destructor.
    HDARNOLD_API
    ~HdArnoldRenderSettings() override;

    /// Hydra 2.0 virtual API: Finalize.
    ///
    /// Called when the prim is removed from the scene.
    ///
    /// @param renderParam The render param.
    HDARNOLD_API
    void Finalize(HdRenderParam* renderParam) override;

    /// Returns whether the render settings prim is valid.
    ///
    /// @return True if valid.
    HDARNOLD_API
    bool IsValid() const;

protected:
    /// Hydra 2.0 virtual API: Sync.
    ///
    /// Syncs the render settings prim.
    ///
    /// @param sceneDelegate The scene delegate.
    /// @param renderParam The render param.
    /// @param dirtyBits The dirty bits.
    HDARNOLD_API
    void _Sync(
        HdSceneDelegate* sceneDelegate,
        HdRenderParam* renderParam,
        const HdDirtyBits* dirtyBits) override;

private:
    /// Updates render products by creating Arnold drivers and configuring outputs.
    ///
    /// @param sceneDelegate The scene delegate.
    /// @param param The Arnold render param.
    void _UpdateRenderProducts(HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param);

    /// Updates the shutter interval on the Arnold options and render param.
    ///
    /// @param sceneDelegate The scene delegate.
    /// @param param The Arnold render param.
    void _UpdateShutterInterval(HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param);

    /// Updates the rendering color space (color manager) on the Arnold options.
    ///
    /// @param sceneDelegate The scene delegate.
    /// @param param The Arnold render param.
    void _UpdateRenderingColorSpace(HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param);

    /// Generates and applies Arnold options from the render settings to the Arnold universe.
    ///
    /// @param sceneDelegate The scene delegate.
    void _UpdateArnoldOptions(HdSceneDelegate* sceneDelegate);

    /// Reads USD render settings and applies them to Arnold options.
    ///
    /// @param sceneDelegate The scene delegate.
    void _ReadUsdRenderSettings(HdSceneDelegate* sceneDelegate);

};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_VERSION >= 2308
