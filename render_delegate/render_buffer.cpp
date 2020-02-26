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
#include "render_buffer.h"

#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3i.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {
template <typename T>
void _ConvertPixel(HdFormat dstFormat, uint8_t* dst, HdFormat srcFormat, uint8_t const* src)
{
    HdFormat srcComponentFormat = HdGetComponentFormat(srcFormat);
    HdFormat dstComponentFormat = HdGetComponentFormat(dstFormat);
    size_t srcComponentCount = HdGetComponentCount(srcFormat);
    size_t dstComponentCount = HdGetComponentCount(dstFormat);

    for (size_t c = 0; c < dstComponentCount; ++c) {
        T readValue = 0;
        if (c < srcComponentCount) {
            if (srcComponentFormat == HdFormatInt32) {
                readValue = ((int32_t*)src)[c];
            } else if (srcComponentFormat == HdFormatFloat16) {
                GfHalf half;
                half.setBits(((uint16_t*)src)[c]);
                readValue = static_cast<float>(half);
            } else if (srcComponentFormat == HdFormatFloat32) {
                readValue = ((float*)src)[c];
            } else if (srcComponentFormat == HdFormatUNorm8) {
                readValue = ((uint8_t*)src)[c] / 255.0f;
            } else if (srcComponentFormat == HdFormatSNorm8) {
                readValue = ((int8_t*)src)[c] / 127.0f;
            }
        }

        if (dstComponentFormat == HdFormatInt32) {
            ((int32_t*)dst)[c] = readValue;
        } else if (dstComponentFormat == HdFormatFloat16) {
            ((uint16_t*)dst)[c] = GfHalf(float(readValue)).bits();
        } else if (dstComponentFormat == HdFormatFloat32) {
            ((float*)dst)[c] = readValue;
        } else if (dstComponentFormat == HdFormatUNorm8) {
            ((uint8_t*)dst)[c] = (readValue * 255.0f);
        } else if (dstComponentFormat == HdFormatSNorm8) {
            ((int8_t*)dst)[c] = (readValue * 127.0f);
        }
    }
}
} // namespace

HdArnoldRenderBuffer::HdArnoldRenderBuffer(const SdfPath& id)
    : HdRenderBuffer(id), _width(0), _height(0), _format(HdFormatInvalid), _pixelSize(0), _mappers(0), _converged(false)
{
}

bool HdArnoldRenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled)
{
    _Deallocate();

    if (dimensions[2] != 1) {
        TF_WARN(
            "Render buffer allocated with dims <%d, %d, %d> and format %s; depth must be 1!", dimensions[0],
            dimensions[1], dimensions[2], TfEnum::GetName(format).c_str());
        return false;
    }

    _width = dimensions[0];
    _height = dimensions[1];
    _format = format;
    _pixelSize = HdDataSizeOfFormat(format);
    _buffer.resize(_width * _height * _pixelSize, 0);

    return true;
}

unsigned int HdArnoldRenderBuffer::GetWidth() const { return _width; }

unsigned int HdArnoldRenderBuffer::GetHeight() const { return _height; }

unsigned int HdArnoldRenderBuffer::GetDepth() const { return 1; }

HdFormat HdArnoldRenderBuffer::GetFormat() const { return _format; }

bool HdArnoldRenderBuffer::IsMultiSampled() const { return false; }

#if USED_USD_VERSION_GREATER_EQ(19, 10)
void* HdArnoldRenderBuffer::Map()
#else
uint8_t* HdArnoldRenderBuffer::Map()
#endif
{
    _mappers++;
    return _buffer.data();
}

void HdArnoldRenderBuffer::Unmap() { _mappers--; }

bool HdArnoldRenderBuffer::IsMapped() const { return _mappers.load() != 0; }

void HdArnoldRenderBuffer::Resolve() {}

bool HdArnoldRenderBuffer::IsConverged() const { return _converged.load(); }

void HdArnoldRenderBuffer::SetConverged(bool cv) { _converged.store(cv); }

void HdArnoldRenderBuffer::Blit(HdFormat format, int width, int height, int offset, int stride, uint8_t const* data)
{
    if (_format == format) {
        if (static_cast<unsigned int>(width) == _width && static_cast<unsigned int>(height) == _height) {
            // Awesome! Blit line by line.
            for (unsigned int j = 0; j < _height; ++j) {
                memcpy(
                    &_buffer[(j * _width) * _pixelSize], &data[(j * stride + offset) * _pixelSize], _width * _pixelSize);
            }
        } else {
            // Ok...  Blit pixel by pixel, with nearest point sampling.
            float scalei = width / float(_width);
            float scalej = height / float(_height);
            for (unsigned int j = 0; j < _height; ++j) {
                for (unsigned int i = 0; i < _width; ++i) {
                    unsigned int ii = scalei * i;
                    unsigned int jj = scalej * j;
                    memcpy(
                        &_buffer[(j * _width + i) * _pixelSize], &data[(jj * stride + offset + ii) * _pixelSize],
                        _pixelSize);
                }
            }
        }
    } else {
        // Convert pixel by pixel, with nearest point sampling.
        // If src and dst are both int-based, don't round trip to float.
        size_t pixelSize = HdDataSizeOfFormat(format);
        bool convertAsInt =
            (HdGetComponentFormat(format) == HdFormatInt32) && (HdGetComponentFormat(_format) == HdFormatInt32);

        float scalei = width / float(_width);
        float scalej = height / float(_height);
        for (unsigned int j = 0; j < _height; ++j) {
            for (unsigned int i = 0; i < _width; ++i) {
                unsigned int ii = scalei * i;
                unsigned int jj = scalej * j;
                if (convertAsInt) {
                    _ConvertPixel<int32_t>(
                        _format, static_cast<uint8_t*>(&_buffer[(j * _width + i) * _pixelSize]), format,
                        &data[(jj * stride + offset + ii) * pixelSize]);
                } else {
                    _ConvertPixel<float>(
                        _format, static_cast<uint8_t*>(&_buffer[(j * _width + i) * _pixelSize]), format,
                        &data[(jj * stride + offset + ii) * pixelSize]);
                }
            }
        }
    }
}

void HdArnoldRenderBuffer::_Deallocate()
{
    _width = 0;
    _height = 0;
    _format = HdFormatInvalid;
    _buffer.resize(0);
    _mappers.store(0);
    _converged.store(false);
}

PXR_NAMESPACE_CLOSE_SCOPE
