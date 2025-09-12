//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2019 Luma Pictures
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
//
// Modifications Copyright 2022 Autodesk, Inc.
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
/// @file render_pass.h
///
/// Utilities for handling Render Passes.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/renderPass.h>

#include "hdarnold.h"

#include "render_buffer.h"
#include "render_delegate.h"

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for handling Render Passes.
class HdArnoldRenderPass : public HdRenderPass {
public:
    /// Constructor for HdArnoldRenderPass.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param index Pointer to the Render Index.
    /// @param collection RPrim Collection to bind for rendering.
    HDARNOLD_API
    HdArnoldRenderPass(
        HdArnoldRenderDelegate* renderDelegate, HdRenderIndex* index, const HdRprimCollection& collection);
    /// Destructor for HdArnoldRenderPass.
    HDARNOLD_API
    ~HdArnoldRenderPass() override;

    /// Returns true if the render has converged.
    ///
    /// @return True if the render has converged.
    bool IsConverged() const override { return _isConverged; }

protected:
    /// Executing the Render Pass.
    ///
    /// This function is continously executed, until IsConverged returns true.
    ///
    /// @param renderPassState Pointer to the Hydra Render Pass State.
    /// @param renderTags List of tags to render, currently unused.
    HDARNOLD_API
    void _Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags) override;

    /// Tells if the aov bindings has changed.
    ///
    /// \param aovBindings Hydra AOV bindings from the render pass.
    /// \return Returns true if the aov bindings are the same, false otherwise.
    HDARNOLD_API
    bool _RenderBuffersChanged(const HdRenderPassAovBindingVector& aovBindings);

    /// Clears render buffers and destroys any assigned filter.
    HDARNOLD_API
    void _ClearRenderBuffers();

private:
    HdArnoldRenderBufferStorage _renderBuffers;   ///< Render buffer storage.
    HdArnoldRenderBufferStorage _fallbackBuffers; ///< Render buffer storage if there are no aov bindings.
    HdArnoldRenderBuffer _fallbackColor;          ///< Color render buffer if there are no aov bindings.
    HdArnoldRenderBuffer _fallbackDepth;          ///< Depth render buffer if there are no aov bindings.
    HdArnoldRenderBuffer _fallbackPrimId;         ///< Prim ID buffer if there are no aov bindings.
    AtArray* _fallbackOutputs;                    ///< AtArray storing the fallback outputs definitions.
    AtArray* _fallbackAovShaders;                 ///< AtArray storing the fallback AOV shaders.
    std::vector<AtNode*> _aovShaders;             ///< Pointer to list of user aov shaders

    HdArnoldRenderDelegate* _renderDelegate; ///< Pointer to the Render Delegate.
    AtNode* _camera = nullptr;               ///< Pointer to the Arnold Camera.
    AtNode* _defaultFilter = nullptr;        ///< Pointer to the default Arnold Filter.
    AtNode* _closestFilter = nullptr;        ///< Pointer to the closest Arnold Filter.
    AtNode* _mainDriver = nullptr;           ///< Pointer to the Arnold Driver writing color, position and depth.
    AtNode* _primIdWriter = nullptr;         ///< Pointer to the Arnold prim ID writer shader.
    AtNode* _primIdReader = nullptr;         ///< Pointer to the Arnold prim ID reader shader.

    struct CustomRenderVar {
        /// Definition for the output string.
        AtString output;
        /// Optional writer node for each AOV.
        AtNode* writer = nullptr;
        /// Optional reader node for each AOV.
        AtNode* reader = nullptr;
    };

    // Each arnold driver can handle multiple AOVs.
    struct CustomProduct {
        /// List of the RenderVars.
        std::vector<CustomRenderVar> renderVars;
        /// Custom driver.
        AtNode* driver = nullptr;
        /// Filter for the custom driver.
        AtNode* filter = nullptr;
    };

    std::vector<CustomProduct> _customProducts; ///< List of Custom Render Products.

    GfMatrix4d _viewMtx; ///< View matrix of the camera.
    GfMatrix4d _projMtx; ///< Projection matrix of the camera.

    CameraUtilFraming _framing;

    // Window NDC region, that can be used for overscan, or to adjust the frustum
    GfVec4f _windowNDC =  GfVec4f(0.f, 0.f, 1.f, 1.f);

    bool _isConverged = false;          ///< State of the render convergence.
    bool _usingFallbackBuffers = false; ///< If the render pass is using the fallback buffers.
};

PXR_NAMESPACE_CLOSE_SCOPE
