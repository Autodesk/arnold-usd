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
/// @file render_buffer.h
///
/// Utilities for handling Hydra Render Buffers.
#pragma once

#include <pxr/pxr.h>

#include "../arnold_usd.h"
#include "api.h"

#include <pxr/imaging/hd/renderBuffer.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for handling Hydra Render Buffers.
///
/// The HdRenderBuffer is for handling 2d images for render output. Since this
/// is handled by the Arnold Core, HdArnoldRenderBuffer is reimplementing the
/// HdRenderBuffer class without doing or allocating anything.
class HdArnoldRenderBuffer : public HdRenderBuffer {
public:
    /// Constructor for HdArnoldRenderBuffer.
    ///
    /// @param id Path to the Render Buffer Primitive.
    HDARNOLD_API
    HdArnoldRenderBuffer(const SdfPath& id);
    /// Destructor for HdArnoldRenderBuffer.
    HDARNOLD_API
    ~HdArnoldRenderBuffer() override = default;

    /// Allocates the memory used by the render buffer.
    ///
    /// Does nothing.
    ///
    /// @param dimensions 3 Dimension Vector describing the dimensions of the
    ///  render buffer.
    /// @param format HdFormat specifying the format of the Render Buffer.
    /// @param multiSampled Boolean to indicate if the Render Buffer is
    ///  multisampled.
    /// @return Boolean to indicate if allocation was successful, always false.
    HDARNOLD_API
    bool Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled) override;

    /// Returns the width of the Render Buffer.
    ///
    /// @return Width of the Render Buffer, always 0.
    HDARNOLD_API
    unsigned int GetWidth() const override;
    /// Returns the height of the Render Buffer.
    ///
    /// @return Height of the Render Buffer, always 0.
    HDARNOLD_API
    unsigned int GetHeight() const override;
    /// Returns the depth of the Render Buffer, always 0.
    ///
    /// @return Depth of the Render Buffer.
    HDARNOLD_API
    unsigned int GetDepth() const override;
    /// Returns the format of the Render Buffer.
    ///
    /// @return Format of the Render Buffer, always UNorm8.
    HDARNOLD_API
    HdFormat GetFormat() const override;
    /// Returns true of if the Render Buffer is multi-sampled, false otherwise.
    ///
    /// @return Boolean indicating if the Render Buffer is multi-sampled, always
    ///  false.
    HDARNOLD_API
    bool IsMultiSampled() const override;
    /// Maps the Render Buffer to the system memory.
    ///
    /// @return Pointer to the Render Buffer mapped to system memory.
    HDARNOLD_API
#if USED_USD_VERSION_GREATER_EQ(19, 10)
    void* Map() override;
#else
    uint8_t* Map() override;
#endif
    /// Unmaps the Render Buffer from the system memory.
    HDARNOLD_API
    void Unmap() override;
    /// Returns true if the Render Buffer is mapped to system memory.
    ///
    /// @return Boolean indicating if the Render Buffer is mapped to system
    ///  memory.
    HDARNOLD_API
    bool IsMapped() const override;

    /// Resolve the buffer so that reads reflect the latest writes.
    ///
    /// Does nothing.
    HDARNOLD_API
    void Resolve() override;
    /// Return whether the buffer is converged or not.
    HDARNOLD_API
    bool IsConverged() const override;
    /// Sets whether the buffer is converged or not.
    void SetConverged(bool cv);

    /// Helper to blit the render buffer to data
    ///
    /// format is the input format
    void Blit(HdFormat format, int width, int height, int offset, int stride, uint8_t const* data);

protected:
    /// Deallocate memory allocated by the Render Buffer.
    ///
    /// Does nothing.
    HDARNOLD_API
    void _Deallocate() override;

private:
    unsigned int _width;
    unsigned int _height;
    HdFormat _format;

    std::vector<uint8_t> _buffer;
    std::atomic<int> _mappers;
    std::atomic<bool> _converged;
};

PXR_NAMESPACE_CLOSE_SCOPE
