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

// memcpy
#include <cstring>

// TOOD(pal): use a more efficient locking mechanism than the std::mutex.
PXR_NAMESPACE_OPEN_SCOPE

HdArnoldRenderBuffer::HdArnoldRenderBuffer(const SdfPath& id) : HdRenderBuffer(id) {}

bool HdArnoldRenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled)
{
    std::lock_guard<std::mutex> _guard(_mutex);
    // So deallocate won't lock.
    decltype(_buffer) tmp{};
    _buffer.swap(tmp);
    TF_UNUSED(multiSampled);
    _format = format;
    _width = dimensions[0];
    _height = dimensions[1];
    const auto byteCount = _width * _height * HdDataSizeOfFormat(format);
    if (byteCount != 0) {
        _buffer.resize(byteCount, 0);
    }
    return true;
}

void* HdArnoldRenderBuffer::Map()
{
    _mutex.lock();
    if (_buffer.empty()) {
        _mutex.unlock();
        return nullptr;
    }
    return _buffer.data();
}

void HdArnoldRenderBuffer::Unmap() { _mutex.unlock(); }

bool HdArnoldRenderBuffer::IsMapped() const { return false; }

void HdArnoldRenderBuffer::_Deallocate()
{
    std::lock_guard<std::mutex> _guard(_mutex);
    decltype(_buffer) tmp{};
    _buffer.swap(tmp);
}

void HdArnoldRenderBuffer::WriteBucket(
    unsigned int bucketXO, unsigned int bucketYO, unsigned int bucketWidth, unsigned int bucketHeight, HdFormat format,
    const void* bucketData)
{
    std::lock_guard<std::mutex> _guard(_mutex);
    const auto xo = AiClamp(bucketXO, 0u, _width);
    const auto xe = AiClamp(bucketXO + bucketWidth, 0u, _width);
    // Empty bucket.
    if (xe == xo) {
        return;
    }
    const auto yo = AiClamp(bucketYO, 0u, _height);
    const auto ye = AiClamp(bucketYO + bucketHeight, 0u, _height);
    // Empty bucket.
    if (ye == yo) {
        return;
    }
    const auto dataWidth = (xe - xo);
    // Single component formats can be:
    //  - HdFormatUNorm8
    //  - HdFormatSNorm8
    //  - HdFormatFloat16
    //  - HdFormatFloat32
    //  - HdformatInt32
    // We need to check if the components formats and counts match.
    // The simplest case is when the component format and the count matches, we can copy per line in this case.
    // If the the component format matches, but the count is different, we are copying as much data as we can
    //  and zeroing out the rest.
    // If the component count matches, but the format is different, we are converting each element.
    // If none of the matches, we are converting as much as we can, and zeroing out the rest.
    const auto componentCount = HdGetComponentCount(_format);
    const auto componentFormat = HdGetComponentFormat(_format);
    // TODO(pal): Implement the cases when formats don't match.
    // For now we are only implementing cases where the format does matches.
    const auto inComponentCount = HdGetComponentCount(format);
    const auto inComponentFormat = HdGetComponentFormat(format);
    if (componentFormat == inComponentFormat) {
        const auto pixelSize = HdDataSizeOfFormat(_format);
        // Copy per line
        if (inComponentCount == componentCount) {
            // The size of the line we are copying.
            const auto lineDataSize = dataWidth * pixelSize;
            // The full size of a line.
            const auto fullLineDataSize = _width * pixelSize;
            // The size of the line for the bucket, this could be more than the data copied.
            const auto inLineDataSize = bucketWidth * pixelSize;
            // This is the first pixel we are copying into.
            auto* data = _buffer.data() + (xo + (_height - yo - 1) * _width) * pixelSize;
            const auto* inData = static_cast<const uint8_t*>(bucketData);
            for (auto y = yo; y < ye; y += 1) {
                memcpy(data, inData, lineDataSize);
                data -= fullLineDataSize;
                inData += inLineDataSize;
            }
        }
    } else { // Need to do conversion.
        return;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
