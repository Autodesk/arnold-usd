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
/// @file render_delegate/render_buffer.h
///
/// Utilities to store and load rendered data.
#pragma once

#include <pxr/pxr.h>

#include "api.h"

#include "hdarnold.h"

#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/texture.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

struct AtNode;

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldRenderDelegate;

/// Utility class for handling render data.
class HdArnoldRenderBuffer : public HdRenderBuffer {
public:
    HDARNOLD_API
    HdArnoldRenderBuffer(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

    HDARNOLD_API
    ~HdArnoldRenderBuffer() override = default;

    HDARNOLD_API
    bool Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled) override;

    /// Get the buffer's width.
    ///
    /// @return The width of the buffer.
    virtual unsigned int GetWidth() const override { return _width; }
    /// Get the buffer's height.
    ///
    /// @return The height of the buffer.
    unsigned int GetHeight() const override { return _height; }
    /// Get the buffer's depth.
    ///
    /// @return Always one, as this is a 2d buffer.
    unsigned int GetDepth() const override { return 1; }
    /// Get the buffer's per-pixel format.
    virtual HdFormat GetFormat() const override { return _format; }

    /// Get whether the buffer is multisampled.
    /// @return True if multisampled, false otherwise.
    bool IsMultiSampled() const override { return true; }

    /// Map the buffer for reading.
    /// @return The render buffer mapped to memory.
    HDARNOLD_API
    void* Map() override;
    /// Unmap the buffer. It is no longer safe to read from the buffer.
    HDARNOLD_API
    void Unmap() override;
    /// Return whether the buffer is currently mapped by anybody.
    ///
    /// @return True if mapped, false otherwise.
    HDARNOLD_API
    bool IsMapped() const override;

    /// Returns the GPU texture backing this render buffer, wrapped in a VtValue.
    /// Returns an empty VtValue when no Hgi was provided (CPU path).
    HDARNOLD_API
    VtValue GetResource(bool multiSampled) const override;


    /// Resolve the buffer so that reads reflect the latest writes.
    /// This buffer does not need any resolving.
    void Resolve() override {}

    /// Return whether the buffer is converged (whether the renderer is
    /// still adding samples or not).
    ///
    /// @return True if converged, false otherwise.
    bool IsConverged() const override { return _converged; }

    /// Sets the convergence of the render buffer.
    ///
    /// @param converged True if the render buffer is converged, false otherwise.
    void SetConverged(bool converged) { _converged = converged; }

    bool IsEmpty() const { return _buffer.empty() && !_texture && !_aovTexture; }

    HDARNOLD_API
    void WriteBucket(
        unsigned int bucketXO, unsigned int bucketYo, unsigned int bucketWidth, unsigned int bucketHeight,
        HdFormat format, const void* bucketData);
    
    /// Utility class for storing render buffers.
    struct BufferDefinition {
        HdAovSettingsMap settings;              ///< Filter and AOV settings for the Render Buffer.
        HdArnoldRenderBuffer* buffer = nullptr; ///< HdArnoldRenderBuffer pointer.
        AtNode* filter = nullptr;               ///< Arnold filter.
        AtNode* writer = nullptr;               ///< Arnold AOV write node for primvar AOVs.
        AtNode* reader = nullptr;               ///< Arnold user data reader for primvar AOVs.

        /// Default constructor.
        BufferDefinition() = default;
        
    };

    /// Provide the host Hgi instance. Must be called before Allocate() to take the GPU path.
    /// Passing nullptr keeps the CPU path active.
    HDARNOLD_API
    void SetHgi(Hgi* hgi);

    /// Ensures the GPU texture has been created with a valid GL id. Call this from a context
    /// where the GL context is current (e.g. the render pass's _Execute). If the texture is
    /// missing or its GL id is 0 (because the previous create-attempt ran without a GL
    /// context), it is destroyed and recreated.
    HDARNOLD_API
    void EnsureGpuTexture();

    /// Returns true if this buffer is backed by a GPU texture.
    bool HasGpuTexture() const { return static_cast<bool>(_texture); }

    /// Provide the Arnold AOV name used when calling AiQueryAOV on this buffer.
    HDARNOLD_API
    void SetAovName(const TfToken& aovName) { _aovName = aovName; }

private:
    /// Deallocates the data stored in the buffer.
    HDARNOLD_API
    void _Deallocate() override;

#ifdef FAST_VIEWPORT_SUPPORT
    /// Blit from _aovTexture into _texture with a Y flip (Arnold top-origin -> OpenGL).
    /// @return True if _texture was updated, false to use _aovTexture as-is.
    bool _FlipAovToDisplayTexture() const;
#endif

    std::vector<uint8_t> _buffer;                    ///< Storing render data (CPU path only).
    Hgi* _hgi = nullptr;                             ///< Borrowed Hgi instance, owned by the render delegate's host.
    mutable HgiTextureHandle _aovTexture;            ///< AiQueryAOV target (Arnold image-space Y).
    mutable HgiTextureHandle _texture;               ///< Hydra-facing texture after Y flip (GPU path only).
    std::atomic<bool> _gpuInit = false;
    std::mutex _mutex;                               ///< Mutex for the parallel writes.
    unsigned int _width = 0;                         ///< Buffer width.
    unsigned int _height = 0;                        ///< Buffer height.
    HdFormat _format = HdFormat::HdFormatUNorm8Vec4; ///< Internal format of the buffer.
    HdArnoldRenderDelegate* _renderDelegate = nullptr; ///< Borrowed delegate pointer for accessing the render session.
    bool _converged = false;                         ///< Store if the render buffer has converged.
    TfToken _aovName;                                ///< AOV name passed to AiQueryAOV.
    bool _mapped = false;                            ///< Whether Map() left the mutex held; consulted by 
};

using HdArnoldRenderBufferStorage =
    std::unordered_map<TfToken, HdArnoldRenderBuffer::BufferDefinition, TfToken::HashFunctor>;

PXR_NAMESPACE_CLOSE_SCOPE
