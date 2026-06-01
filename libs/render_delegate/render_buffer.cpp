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
#include "render_buffer.h"
#include "render_delegate.h"
#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hgiGL/texture.h>

#include <pxr/base/gf/half.h>
#include <pxr/base/gf/vec3i.h>

#include <ai.h>

// memcpy
#include <cstring>
#include <string>

#include <iostream>
#ifdef FAST_VIEWPORT_SUPPORT
#include <pxr/imaging/garch/glApi.h>
#endif

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

    const auto copyOp = [](const typename HdFormatType<FROM>::type& in) -> typename HdFormatType<TO>::type
    {
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

WriteBucketFunctionMap writeBucketFunctions{
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

HdArnoldRenderBuffer::HdArnoldRenderBuffer(HdArnoldRenderDelegate* renderDelegate, 
    const SdfPath& id) : HdRenderBuffer(id), _renderDelegate(renderDelegate)
{
}

namespace {

// Attempts to (re)create the HgiGL texture. Returns the GL texture id, which may be 0 if
// no GL context was current at call time. Caller holds the buffer's mutex.
//
// The texture format hard-coded to HgiFormatFloat32Vec4 (GL_RGBA32F) regardless of the
// Hd-side format, because that is what AiQueryAOV expects to write into. The Hd-side
// _format is unchanged so CPU consumers (and Hydra's format introspection) keep seeing
// what was requested; the actual GL texture sampled by the compositor reads its format
// from the Hgi descriptor, which is always RGBA32F.
uint32_t _CreateGpuTexture(
    Hgi* hgi, HgiTextureHandle& outTexture, unsigned int width, unsigned int height, HdFormat /*format*/,
    const TfToken& aovName, const char* suffix)
{
    if (hgi == nullptr) return 0;
    if (width == 0 || height == 0) return 0;
    HgiTextureDesc desc;
    std::string debugName = aovName.IsEmpty() ? "HdArnoldRenderBuffer" : aovName.GetString();
    if (suffix != nullptr && suffix[0] != '\0') {
        debugName += '.';
        debugName += suffix;
    }
    desc.debugName = debugName;
    desc.type = HgiTextureType2D;
    desc.dimensions = GfVec3i(static_cast<int>(width), static_cast<int>(height), 1);
    desc.format = HgiFormatFloat32Vec4;
    desc.layerCount = 1;
    desc.mipLevels = 1;
    desc.sampleCount = HgiSampleCount1;
    desc.usage = HgiTextureUsageBitsShaderRead | HgiTextureUsageBitsColorTarget;
    outTexture = hgi->CreateTexture(desc);
    if (auto* gl = dynamic_cast<HgiGLTexture*>(outTexture.Get())) {
        return gl->GetTextureId();
    }
    return 0;
}

uint32_t _GetGlTextureId(const HgiTextureHandle& texture)
{
    if (auto* gl = dynamic_cast<HgiGLTexture*>(texture.Get())) {
        return gl->GetTextureId();
    }
    return 0;
}

#ifdef FAST_VIEWPORT_SUPPORT

// Fullscreen blit with vertex-shader Y flip (Arnold top-origin -> OpenGL bottom-origin).
struct _GpuFlipBlit {
    GLuint program = 0;
    GLuint vertexBuffer = 0;

    void _CompileShader(const char* src, GLenum stage, GLuint* outShader) const
    {
        *outShader = glCreateShader(stage);
        glShaderSource(*outShader, 1, &src, nullptr);
        glCompileShader(*outShader);
        GLint status = 0;
        glGetShaderiv(*outShader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
            TF_WARN("HdArnoldRenderBuffer: GPU Y-flip shader compile failed");
        }
    }

    void _EnsureInitialized()
    {
        if (program != 0) {
            return;
        }

        static const char* const kVertexShader120 = R"(#version 120
attribute vec4 position;
attribute vec2 uvIn;
varying vec2 uv;
void main(void)
{
    gl_Position = position;
    uv = vec2(uvIn.x, 1.0 - uvIn.y);
}
)";

        static const char* const kFragmentShader120 = R"(#version 120
varying vec2 uv;
uniform sampler2D colorIn;
void main(void)
{
    gl_FragColor = texture2D(colorIn, uv);
}
)";

        GLuint vs = 0;
        GLuint fs = 0;
        _CompileShader(kVertexShader120, GL_VERTEX_SHADER, &vs);
        _CompileShader(kFragmentShader120, GL_FRAGMENT_SHADER, &fs);

        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        GLint linkStatus = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            TF_WARN("HdArnoldRenderBuffer: GPU Y-flip shader link failed");
        }
        glDeleteShader(vs);
        glDeleteShader(fs);

        static const float vertices[] = {
            // position (xyzw)   uv
            -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 2.0f,
            -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            3.0f, -1.0f, 0.0f, 1.0f, 2.0f, 0.0f,
        };
        glGenBuffers(1, &vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    bool Flip(uint32_t srcTextureId, uint32_t dstTextureId, int width, int height)
    {
        if (srcTextureId == 0 || dstTextureId == 0 || width <= 0 || height <= 0) {
            return false;
        }

        _EnsureInitialized();
        if (program == 0) {
            return false;
        }

        GLint restoreFramebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &restoreFramebuffer);

        GLint restoreViewport[4];
        glGetIntegerv(GL_VIEWPORT, restoreViewport);

        GLint restoreProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &restoreProgram);

        GLint restoreActiveTexture = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &restoreActiveTexture);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTextureId, 0);

        const GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
            TF_WARN("HdArnoldRenderBuffer: GPU Y-flip FBO incomplete (status %u)", static_cast<unsigned>(fboStatus));
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(restoreFramebuffer));
            glDeleteFramebuffers(1, &fbo);
            return false;
        }

        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, srcTextureId);
        const GLint colorLoc = glGetUniformLocation(program, "colorIn");
        glUniform1i(colorLoc, 0);

        GLint restoreArrayBuffer = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &restoreArrayBuffer);

        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        const GLint locPosition = glGetAttribLocation(program, "position");
        glVertexAttribPointer(locPosition, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 6, nullptr);
        glEnableVertexAttribArray(locPosition);

        const GLint locUv = glGetAttribLocation(program, "uvIn");
        glVertexAttribPointer(
            locUv, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 6, reinterpret_cast<void*>(sizeof(float) * 4));
        glEnableVertexAttribArray(locUv);

        GLboolean restoreDepthEnabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean restoreDepthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &restoreDepthMask);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        GLboolean restoreBlend = glIsEnabled(GL_BLEND);
        glDisable(GL_BLEND);

        glDrawArrays(GL_TRIANGLES, 0, 3);

        if (restoreBlend) {
            glEnable(GL_BLEND);
        }
        if (restoreDepthEnabled) {
            glEnable(GL_DEPTH_TEST);
        }
        glDepthMask(restoreDepthMask);

        glDisableVertexAttribArray(locPosition);
        glDisableVertexAttribArray(locUv);
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(restoreArrayBuffer));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);

        glUseProgram(static_cast<GLuint>(restoreProgram));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(restoreFramebuffer));
        glViewport(restoreViewport[0], restoreViewport[1], restoreViewport[2], restoreViewport[3]);
        glActiveTexture(restoreActiveTexture);

        glDeleteFramebuffers(1, &fbo);
        return true;
    }
};

_GpuFlipBlit _gpuFlipBlit;

#endif // FAST_VIEWPORT_SUPPORT

} // namespace

bool HdArnoldRenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled)
{
    std::lock_guard<std::mutex> _guard(_mutex);
    // So deallocate won't lock.
    decltype(_buffer) tmp{};
    _buffer.swap(tmp);
    if (_hgi != nullptr) {
        if (_aovTexture) {
            _hgi->DestroyTexture(&_aovTexture);
        }
        if (_texture) {
            _hgi->DestroyTexture(&_texture);
        }
    }
    if (!_SupportedComponentFormat(format)) {
        _width = 0;
        _height = 0;
        return false;
    }
    _gpuInit = false;
    TF_UNUSED(multiSampled);
    _format = format;
    _width = dimensions[0];
    _height = dimensions[1];

    if (_hgi != nullptr) {
        // --GPU buffers--
        // Best-effort: try to create the GPU texture now. If GL context isn't current the
        // resulting GL id will be 0 — the render pass will call EnsureGpuTexture() later
        // from a GL-active context to retry.
        _CreateGpuTexture(_hgi, _aovTexture, _width, _height, _format, _aovName, "aov");
        _CreateGpuTexture(_hgi, _texture, _width, _height, _format, _aovName, "display");
    } 

    // --CPU buffers--
    const auto byteCount = _width * _height * HdDataSizeOfFormat(format);
    if (byteCount != 0) {
        _buffer.resize(byteCount, 0);
    }
    return true;
}

void HdArnoldRenderBuffer::EnsureGpuTexture()
{
    if (_hgi == nullptr || _width == 0 || _height == 0 || _gpuInit) {
        return;
    }
    std::lock_guard<std::mutex> _guard(_mutex);

    if (_aovTexture && _texture && _GetGlTextureId(_aovTexture) != 0 && _GetGlTextureId(_texture) != 0) {
        _gpuInit = true;
        return;
    }

    const auto ensureTexture = [&](HgiTextureHandle& tex, const char* suffix) {
        if (tex && _GetGlTextureId(tex) != 0) {
            return;
        }
        if (tex) {
            _hgi->DestroyTexture(&tex);
        }
        _CreateGpuTexture(_hgi, tex, _width, _height, _format, _aovName, suffix);
    };

    ensureTexture(_aovTexture, "aov");
    ensureTexture(_texture, "display");
    if (_GetGlTextureId(_aovTexture) != 0 && _GetGlTextureId(_texture) != 0) {
        _gpuInit = true;
    }
}

#ifdef FAST_VIEWPORT_SUPPORT
bool HdArnoldRenderBuffer::_FlipAovToDisplayTexture() const
{
    const uint32_t srcId = _GetGlTextureId(_aovTexture);
    const uint32_t dstId = _GetGlTextureId(_texture);
    return _gpuFlipBlit.Flip(srcId, dstId, static_cast<int>(_width), static_cast<int>(_height));
}
#endif
VtValue HdArnoldRenderBuffer::GetResource(bool /*multiSampled*/) const
{
#ifdef FAST_VIEWPORT_SUPPORT
    
    // GetResource() is called by Hydra/Solaris from the main thread with the GL context
    // current. AiQueryAOV does CUDA<->GL interop that requires a current GL context, so
    // this is the right place to pull Arnold's latest AOV data into the GL texture.
    if (_renderDelegate != nullptr && _renderDelegate->IsFastViewport()) {
        auto* self = const_cast<HdArnoldRenderBuffer*>(this);
        self->EnsureGpuTexture();
        std::lock_guard<std::mutex> guard(self->_mutex);
        if (_aovTexture && _texture) {
            const uint64_t aovGlId = static_cast<uint64_t>(_GetGlTextureId(_aovTexture));
            if (aovGlId != 0) {
                const AtRenderErrorCode rc = AiQueryAOV(
                    _renderDelegate->GetRenderSession(), AtString(_aovName.GetText()), aovGlId);
                if (rc == AI_SUCCESS) {
                    // Sync CUDA/GL interop before sampling the AOV texture in our blit.
                    glFinish();
                    if (self->_FlipAovToDisplayTexture() && _GetGlTextureId(_texture) != 0) {
                        return VtValue(_texture);
                    }
                    
                } else {
                    TF_WARN(
                        "AiQueryAOV failed for AOV \"%s\" (code %d)", _aovName.GetText(), static_cast<int>(rc));
                }
            }
            // Flip failed or display texture unavailable — show AOV as-is (may be Y-inverted).
            return VtValue(_aovTexture);
        }
    }

#endif
    return VtValue();
}

void HdArnoldRenderBuffer::SetHgi(Hgi* hgi) { 
    if (_hgi != hgi)
        _gpuInit = false;

    _hgi = hgi; 
}

void* HdArnoldRenderBuffer::Map()
{
    _mutex.lock();
    if (_buffer.empty()) {
        // Leaving the mutex unlocked here means a subsequent Unmap() must NOT
        // try to release it. Track the locked-ness in _mapped (guarded by the
        // mutex while we still hold it) so Unmap doesn't read _buffer.empty()
        // racily — a concurrent Allocate() can flip that between Map and Unmap
        // and would otherwise lead us to unlock a mutex we don't hold (UB).
        _mapped = false;
        _mutex.unlock();
        return nullptr;
    }
    _mapped = true;
    return _buffer.data();
}

void HdArnoldRenderBuffer::Unmap()
{
    if (_mapped) {
        _mapped = false;
        _mutex.unlock();
    }
}

bool HdArnoldRenderBuffer::IsMapped() const { return false; }

void HdArnoldRenderBuffer::_Deallocate()
{
    std::lock_guard<std::mutex> _guard(_mutex);
    decltype(_buffer) tmp{};
    _buffer.swap(tmp);
    if (_hgi != nullptr) {
        if (_aovTexture) {
            _hgi->DestroyTexture(&_aovTexture);
        }
        if (_texture) {
            _hgi->DestroyTexture(&_texture);
        }
    }
    _gpuInit = false;
}

void HdArnoldRenderBuffer::WriteBucket(
    unsigned int bucketXO, unsigned int bucketYO, unsigned int bucketWidth, unsigned int bucketHeight, HdFormat format,
    const void* bucketData)
{
    // When backed by a GPU texture, bucket data is delivered via AiQueryAOV, not the driver path.
    if (_hgi != nullptr)
        return;
    
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
