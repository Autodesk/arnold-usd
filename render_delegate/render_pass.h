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
// Modifications Copyright 2019 Autodesk, Inc.
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
#include <pxr/imaging/hd/renderPass.h>
#ifdef USD_HAS_FULLSCREEN_SHADER
#include <pxr/imaging/hdx/fullscreenShader.h>
#else
#include <pxr/imaging/hdx/compositor.h>
#endif

#include "render_buffer.h"
#include "render_delegate.h"

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for handling Render Passes.
class HdArnoldRenderPass : public HdRenderPass {
public:
    /// Constructor for HdArnoldRenderPass.
    ///
    /// @param delegate Pointer to the Render Delegate.
    /// @param index Pointer to the Render Index.
    /// @param collection RPrim Collection to bind for rendering.
    HDARNOLD_API
    HdArnoldRenderPass(HdArnoldRenderDelegate* delegate, HdRenderIndex* index, const HdRprimCollection& collection);
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

private:
    HdArnoldRenderBufferStorage _renderBuffers;   ///< Render buffer storage.
    HdArnoldRenderBufferStorage _fallbackBuffers; ///< Render buffer storage if there are no aov bindings.
    HdArnoldRenderBuffer _fallbackColor;          ///< Color render buffer if there are no aov bindings.
    HdArnoldRenderBuffer _fallbackDepth;          ///< Depth render buffer if there are no aov bindings.
    HdArnoldRenderBuffer _fallbackPrimId;         ///< Prim ID buffer if there are no aov bindings.
    AtArray* _fallbackOutputs;                    ///< AtArray storing the fallback outputs definitions.

    HdArnoldRenderDelegate* _delegate; ///< Pointer to the Render Delegate.
    AtNode* _camera = nullptr;         ///< Pointer to the Arnold Camera.
    AtNode* _beautyFilter = nullptr;   ///< Pointer to the beauty Arnold Filter.
    AtNode* _closestFilter = nullptr;  ///< Pointer to the closest Arnold Filter.
    AtNode* _driver = nullptr;         ///< Pointer to the Arnold Driver.

#ifdef USD_HAS_FULLSCREEN_SHADER
    HdxFullscreenShader _fullscreenShader; ///< Hydra utility to blit to OpenGL.
#else
    HdxCompositor _compositor; ///< Hydra compositor to blit to OpenGL.
#endif

    GfMatrix4d _viewMtx; ///< View matrix of the camera.
    GfMatrix4d _projMtx; ///< Projection matrix of the camera.

    int _width = 0;  ///< Width of the render buffer.
    int _height = 0; ///< Height of the render buffer.

    bool _isConverged = false;          ///< State of the render convergence.
    bool _usingFallbackBuffers = false; ///< If the render pass is using the fallback buffers.
};

PXR_NAMESPACE_CLOSE_SCOPE
