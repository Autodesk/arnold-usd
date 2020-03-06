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

#include "nodes/nodes.h"
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
    bool IsConverged() const { return _isConverged; }

protected:
    /// Executing the Render Pass.
    ///
    /// This function is continously executed, until IsConverged returns true.
    ///
    /// @param renderPassState Pointer to the Hydra Render Pass State.
    /// @param renderTags List of tags to render, currently unused.
    HDARNOLD_API
    void _Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags) override;

    /// Tells if the OptiX Denoiser is enabled.
    ///
    /// @return Boolean indicating if the OptiX Denoiser is enabled.
    HDARNOLD_API
    bool _GetEnableOptixDenoiser();

private:
    std::vector<AtRGBA8> _colorBuffer;          ///< Memory to store the beauty.
    std::vector<float> _depthBuffer;            ///< Memory to store the depth.
    std::vector<int32_t> _primIdBuffer;         ///< Memory to store the primId.
    HdArnoldRenderDelegate* _delegate;          ///< Pointer to the Render Delegate.
    AtNode* _camera = nullptr;                  ///< Pointer to the Arnold Camera.
    AtNode* _beautyFilter = nullptr;            ///< Pointer to the beauty Arnold Filter.
    AtNode* _closestFilter = nullptr;           ///< Pointer to the closest Arnold Filter.
    AtNode* _denoiserFilter = nullptr;          ///< Poitner to the optix denoiser Arnold filter.
    AtNode* _driver = nullptr;                  ///< Pointer to the Arnold Driver.
    AtArray* _outputsWithoutDenoiser = nullptr; ///< Output definitions without the denoiser.
    AtArray* _outputsWithDenoiser = nullptr;    ///< Output definitions with the denoiser.

#ifdef USD_HAS_FULLSCREEN_SHADER
    HdxFullscreenShader _fullscreenShader; ///< Hydra utility to blit to OpenGL.
#else
    HdxCompositor _compositor; ///< Hydra compositor to blit to OpenGL.
#endif

    GfMatrix4d _viewMtx; ///< View matrix of the camera.
    GfMatrix4d _projMtx; ///< Projection matrix of the camera.

    int _width = 0;  ///< Width of the render buffer.
    int _height = 0; ///< Height of the render buffer.

    bool _isConverged = false;        ///< State of the render convergence.
    bool _optixDenoiserInUse = false; ///< If the OptiX denoiser is in use or not.
    bool _gpuSupportEnabled = false;  ///< If the GPU backend is supported.
};

PXR_NAMESPACE_CLOSE_SCOPE
