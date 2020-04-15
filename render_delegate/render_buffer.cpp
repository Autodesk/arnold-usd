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

#include <pxr/base/gf/half.h>
#include <pxr/base/gf/vec3i.h>

#include <ai.h>

// memcpy
#include <cstring>

// TOOD(pal): use a more efficient locking mechanism than the std::mutex.
PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Mapping the HdFormat base type to a C++ type.
// The function querying the component size is not constexpr.
template <int TYPE>
struct HdFormatType {
    using type = void;
};

template <>
struct HdFormatType<HdFormatUNorm8> {
    using type = uint8_t;
};

template <>
struct HdFormatType<HdFormatSNorm8> {
    using type = int8_t;
};

template <>
struct HdFormatType<HdFormatFloat16> {
    using type = GfHalf;
};

template <>
struct HdFormatType<HdFormatFloat32> {
    using type = float;
};

template <>
struct HdFormatType<HdFormatInt32> {
    using type = int32_t;
};

// We are storing the function pointers in an unordered map and using a very simple, well packed key to look them up.
// We need to investigate if the overhead of the unordered_map lookup, the function call and pushing the arguments
// to the stack are significant, compared to inlining all the functions.
struct ConversionKey {
    const uint16_t from;
    const uint16_t to;
    ConversionKey(int _from, int _to) : from(static_cast<uint16_t>(_from)), to(static_cast<uint16_t>(_to)) {}
    struct HashFunctor {
        size_t operator()(const ConversionKey& key) const
        {
            // The max value for the key is 20.
            // TODO(pal): Use HdFormatCount to better pack the keys.
            return key.to | (key.from << 8);
        }
    };
};

inline bool operator==(const ConversionKey& a, const ConversionKey& b) { return a.from == b.from && a.to == b.to; }

inline bool _SupportedComponentFormat(HdFormat format)
{
    const auto componentFormat = HdGetComponentFormat(format);
    return componentFormat == HdFormatUNorm8 || componentFormat == HdFormatSNorm8 ||
           componentFormat == HdFormatFloat16 || componentFormat == HdFormatFloat32 || componentFormat == HdFormatInt32;
}

template <typename TO, typename FROM>
inline TO _ConvertType(FROM from)
{
    return static_cast<TO>(from);
}

// TODO(pal): Dithering?
template <>
inline uint8_t _ConvertType(float from)
{
    return std::max(0, std::min(static_cast<int>(from * 255.0f), 255));
}

template <>
inline uint8_t _ConvertType(GfHalf from)
{
    return std::max(0, std::min(static_cast<int>(from * 255.0f), 255));
}

template <>
inline int8_t _ConvertType(float from)
{
    return std::max(-127, std::min(static_cast<int>(from * 127.0f), 127));
}

template <>
inline int8_t _ConvertType(GfHalf from)
{
    return std::max(-127, std::min(static_cast<int>(from * 127.0f), 127));
}

// xo, xe, yo, ye is already clamped against width and height and we checked corner cases when the bucket is empty.
template <int TO, int FROM>
inline void _WriteBucket(
    void* buffer, size_t componentCount, unsigned int width, unsigned int height, const void* bucketData,
    size_t bucketComponentCount, unsigned int xo, unsigned int xe, unsigned int yo, unsigned int ye,
    unsigned int bucketWidth)
{
    auto* to =
        static_cast<typename HdFormatType<TO>::type*>(buffer) + (xo + (height - yo - 1) * width) * componentCount;
    const auto* from = static_cast<const typename HdFormatType<FROM>::type*>(bucketData);

    const auto toStep = width * componentCount;
    const auto fromStep = bucketWidth * bucketComponentCount;

    const auto copyOp = [](const typename HdFormatType<FROM>::type& in) -> typename HdFormatType<TO>::type {
        return _ConvertType<typename HdFormatType<TO>::type, typename HdFormatType<FROM>::type>(in);
    };
    const auto dataWidth = xe - xo;
    // We use std::transform instead of std::copy, so we can add special logic for float32/float16. If the lambda is
    // just a straight copy, the behavior should be the same since we can't use memcpy.
    if (componentCount == bucketComponentCount) {
        const auto copyWidth = dataWidth * componentCount;
        for (auto y = yo; y < ye; y += 1) {
            std::transform(from, from + copyWidth, to, copyOp);
            to -= toStep;
            from += fromStep;
        }
    } else { // We need to call std::transform per pixel with the amount of components to copy.
        const auto componentsToCopy = std::min(componentCount, bucketComponentCount);
        for (auto y = yo; y < ye; y += 1) {
            for (auto x = decltype(dataWidth){0}; x < dataWidth; x += 1) {
                std::transform(
                    from + x * bucketComponentCount, from + x * bucketComponentCount + componentsToCopy,
                    to + x * componentCount, copyOp);
            }
            to -= toStep;
            from += fromStep;
        }
    }
}

using WriteBucketFunction = void (*)(
    void*, size_t, unsigned int, unsigned int, const void*, size_t, unsigned int, unsigned int, unsigned int,
    unsigned int, unsigned int);

using WriteBucketFunctionMap = std::unordered_map<ConversionKey, WriteBucketFunction, ConversionKey::HashFunctor>;

WriteBucketFunctionMap writeBucketFunctions {
    // Write to UNorm8 format.
    {{HdFormatUNorm8, HdFormatSNorm8}, _WriteBucket<HdFormatUNorm8, HdFormatSNorm8>},
    {{HdFormatUNorm8, HdFormatFloat16}, _WriteBucket<HdFormatUNorm8, HdFormatFloat16>},
    {{HdFormatUNorm8, HdFormatFloat32}, _WriteBucket<HdFormatUNorm8, HdFormatFloat32>},
    {{HdFormatUNorm8, HdFormatInt32}, _WriteBucket<HdFormatUNorm8, HdFormatInt32>},
    // Write to SNorm8 format.
    {{HdFormatSNorm8, HdFormatUNorm8}, _WriteBucket<HdFormatSNorm8, HdFormatUNorm8>},
    {{HdFormatSNorm8, HdFormatFloat16}, _WriteBucket<HdFormatSNorm8, HdFormatFloat16>},
    {{HdFormatSNorm8, HdFormatFloat32}, _WriteBucket<HdFormatSNorm8, HdFormatFloat32>},
    {{HdFormatSNorm8, HdFormatInt32}, _WriteBucket<HdFormatSNorm8, HdFormatInt32>},
    // Write to Float16 format.
    {{HdFormatFloat16, HdFormatSNorm8}, _WriteBucket<HdFormatFloat16, HdFormatSNorm8>},
    {{HdFormatFloat16, HdFormatUNorm8}, _WriteBucket<HdFormatFloat16, HdFormatUNorm8>},
    {{HdFormatFloat16, HdFormatFloat32}, _WriteBucket<HdFormatFloat16, HdFormatFloat32>},
    {{HdFormatFloat16, HdFormatInt32}, _WriteBucket<HdFormatFloat16, HdFormatInt32>},
    // Write to Float32 format.
    {{HdFormatFloat32, HdFormatSNorm8}, _WriteBucket<HdFormatFloat32, HdFormatSNorm8>},
    {{HdFormatFloat32, HdFormatUNorm8}, _WriteBucket<HdFormatFloat32, HdFormatUNorm8>},
    {{HdFormatFloat32, HdFormatFloat16}, _WriteBucket<HdFormatFloat32, HdFormatFloat16>},
    {{HdFormatFloat32, HdFormatInt32}, _WriteBucket<HdFormatFloat32, HdFormatInt32>},
    // Write to Int32 format.
    {{HdFormatInt32, HdFormatSNorm8}, _WriteBucket<HdFormatInt32, HdFormatSNorm8>},
    {{HdFormatInt32, HdFormatUNorm8}, _WriteBucket<HdFormatInt32, HdFormatUNorm8>},
    {{HdFormatInt32, HdFormatFloat16}, _WriteBucket<HdFormatInt32, HdFormatFloat16>},
    {{HdFormatInt32, HdFormatFloat32}, _WriteBucket<HdFormatInt32, HdFormatFloat32>},
};

} // namespace

HdArnoldRenderBuffer::HdArnoldRenderBuffer(const SdfPath& id) : HdRenderBuffer(id)
{
    _hasUpdates.store(false, std::memory_order_release);
}

bool HdArnoldRenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled)
{
    std::lock_guard<std::mutex> _guard(_mutex);
    // So deallocate won't lock.
    decltype(_buffer) tmp{};
    _buffer.swap(tmp);
    if (!_SupportedComponentFormat(format)) {
        _width = 0;
        _height = 0;
        return false;
    }
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

#ifdef USD_HAS_UPDATED_RENDER_BUFFER
void* HdArnoldRenderBuffer::Map()
#else
uint8_t* HdArnoldRenderBuffer::Map()
#endif
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
    if (!_SupportedComponentFormat(format)) {
        return;
    }
    std::lock_guard<std::mutex> _guard(_mutex);
    // Checking for empty buffers.
    if (_buffer.empty()) {
        return;
    }
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
    _hasUpdates.store(true, std::memory_order_release);
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
    // For now we are only implementing cases where the format does matches.
    const auto inComponentCount = HdGetComponentCount(format);
    const auto inComponentFormat = HdGetComponentFormat(format);
    if (componentFormat == inComponentFormat) {
        const auto pixelSize = HdDataSizeOfFormat(_format);
        // Copy per line
        const auto lineDataSize = dataWidth * pixelSize;
        // The size of the line we are copying.
        // The full size of a line.
        const auto fullLineDataSize = _width * pixelSize;
        // This is the first pixel we are copying into.
        auto* data = _buffer.data() + (xo + (_height - yo - 1) * _width) * pixelSize;
        const auto* inData = static_cast<const uint8_t*>(bucketData);
        if (inComponentCount == componentCount) {
            // The size of the line for the bucket, this could be more than the data copied.
            const auto inLineDataSize = bucketWidth * pixelSize;
            // This is the first pixel we are copying into.
            for (auto y = yo; y < ye; y += 1) {
                memcpy(data, inData, lineDataSize);
                data -= fullLineDataSize;
                inData += inLineDataSize;
            }
        } else {
            // Component counts do not match, we need to copy as much data as possible and leave the rest to their
            // default values, we expect someone to set that up before this call.
            const auto copiedDataSize =
                std::min(inComponentCount, componentCount) * HdDataSizeOfFormat(componentFormat);
            // The pixelSize is different for the incoming data.
            const auto inPixelSize = HdDataSizeOfFormat(format);
            // The size of the line for the bucket, this could be more than the data copied.
            const auto inLineDataSize = bucketWidth * inPixelSize;
            for (auto y = yo; y < ye; y += 1) {
                for (auto x = decltype(dataWidth){0}; x < dataWidth; x += 1) {
                    memcpy(data + x * pixelSize, inData + x * inPixelSize, copiedDataSize);
                }
                data -= fullLineDataSize;
                inData += inLineDataSize;
            }
        }
    } else { // Need to do conversion.
        const auto it = writeBucketFunctions.find({componentFormat, inComponentFormat});
        if (it != writeBucketFunctions.end()) {
            it->second(
                _buffer.data(), componentCount, _width, _height, bucketData, inComponentCount, xo, xe, yo, ye,
                bucketWidth);
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
