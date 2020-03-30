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
/// @file render_delegate/render_buffer.h
///
/// Utilities to store and load rendered data.
#pragma once

#include <pxr/pxr.h>

#include "../arnold_usd.h"
#include "api.h"

#include "hdarnold.h"

#include <pxr/imaging/hd/renderBuffer.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for handling render data.
class HdArnoldRenderBuffer : public HdRenderBuffer {
public:
    HDARNOLD_API
    HdArnoldRenderBuffer(const SdfPath& id);

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
#ifdef USD_HAS_UPDATED_RENDER_BUFFER
    void* Map() override;
#else
    uint8_t* Map() override;
#endif
    /// Unmap the buffer. It is no longer safe to read from the buffer.
    HDARNOLD_API
    void Unmap() override;
    /// Return whether the buffer is currently mapped by anybody.
    ///
    /// @return True if mapped, false otherwise.
    HDARNOLD_API
    bool IsMapped() const override;

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

    HDARNOLD_API
    void WriteBucket(
        unsigned int bucketXO, unsigned int bucketYo, unsigned int bucketWidth, unsigned int bucketHeight,
        HdFormat format, const void* bucketData);

    /// Return wether or not the buffer has any updates. The function also resets the internal counter tracking the
    /// if there has been any updates.
    ///
    /// @return True if the buffer has any updates, false otherwise.
    bool HasUpdates() { return _hasUpdates.exchange(false, std::memory_order_acq_rel); }

private:
    /// Deallocates the data stored in the buffer.
    HDARNOLD_API
    void _Deallocate() override;

    std::vector<uint8_t> _buffer;                    ///< Storing render data.
    std::mutex _mutex;                               ///< Mutex for the parallel writes.
    unsigned int _width = 0;                         ///< Buffer width.
    unsigned int _height = 0;                        ///< Buffer height.
    HdFormat _format = HdFormat::HdFormatUNorm8Vec4; ///< Internal format of the buffer.
    bool _converged = false;                         ///< Store if the render buffer has converged.
    std::atomic<bool> _hasUpdates;                   ///< If the render buffer has any updates.
};

using HdArnoldRenderBufferStorage = std::unordered_map<TfToken, HdArnoldRenderBuffer*, TfToken::HashFunctor>;

PXR_NAMESPACE_CLOSE_SCOPE
