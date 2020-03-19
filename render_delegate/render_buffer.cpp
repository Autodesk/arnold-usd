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
#include "render_buffer.h"

#include <pxr/base/gf/vec3i.h>

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

HdArnoldRenderBuffer::HdArnoldRenderBuffer(const SdfPath& id) : HdRenderBuffer(id) {}

bool HdArnoldRenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled)
{
    _Deallocate();
    TF_UNUSED(multiSampled);
    _width = dimensions[0];
    _height = dimensions[1];
    const auto byteCount = _width * _height * HdDataSizeOfFormat(format);
    if (byteCount != 0) {
        _buffer.resize(byteCount, 0);
    }
    return true;
}

void* HdArnoldRenderBuffer::Map() { return _buffer.data(); }

void HdArnoldRenderBuffer::Unmap() {}

bool HdArnoldRenderBuffer::IsMapped() const { return false; }

void HdArnoldRenderBuffer::_Deallocate()
{
    decltype(_buffer) tmp{};
    _buffer.swap(tmp);
}

void HdArnoldRenderBuffer::WriteBucket(
    unsigned int x, unsigned int y, unsigned int width, unsigned int height, HdFormat format, const void* data)
{
    // TODO(pal): Implement
}

PXR_NAMESPACE_CLOSE_SCOPE
