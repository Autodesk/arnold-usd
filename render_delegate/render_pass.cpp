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
/*
 * TODO:
 * - Writing to the render buffers directly.
 */
#include "render_pass.h"

#include <pxr/base/tf/staticTokens.h>

#include <pxr/imaging/hd/renderPassState.h>

#include <algorithm>
#include <cstring> // memset

#include "config.h"
#include "constant_strings.h"
#include "nodes/nodes.h"
#include "render_buffer.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (color)
    (depth));
// clang-format on

HdArnoldRenderPass::HdArnoldRenderPass(
    HdArnoldRenderDelegate* delegate, HdRenderIndex* index, const HdRprimCollection& collection)
    : HdRenderPass(index, collection), _delegate(delegate)
{
    {
        AtString reason;
#if AI_VERSION_ARCH_NUM > 5
        _gpuSupportEnabled = AiDeviceTypeIsSupported(AI_DEVICE_TYPE_GPU, reason);
#endif
    }
    auto* universe = _delegate->GetUniverse();
    _camera = AiNode(universe, str::persp_camera);
    AiNodeSetPtr(AiUniverseGetOptions(universe), str::camera, _camera);
    AiNodeSetStr(_camera, str::name, _delegate->GetLocalNodeName(str::renderPassCamera));
    _beautyFilter = AiNode(universe, str::box_filter);
    AiNodeSetStr(_beautyFilter, str::name, _delegate->GetLocalNodeName(str::renderPassFilter));
    _closestFilter = AiNode(universe, str::closest_filter);
    AiNodeSetStr(_closestFilter, str::name, _delegate->GetLocalNodeName(str::renderPassClosestFilter));
    _driver = AiNode(universe, HdArnoldNodeNames::driver);
    AiNodeSetStr(_driver, str::name, _delegate->GetLocalNodeName(str::renderPassDriver));
    auto* options = _delegate->GetOptions();
    _outputsWithoutDenoiser = AiArrayAllocate(3, 1, AI_TYPE_STRING);
    _outputsWithDenoiser = AiArrayAllocate(4, 1, AI_TYPE_STRING);
    const auto beautyString = TfStringPrintf("RGBA RGBA %s %s", AiNodeGetName(_beautyFilter), AiNodeGetName(_driver));
    const auto denoiserString = TfStringPrintf(
        "RGBA_denoise RGBA %s %s", _delegate->GetLocalNodeName(str::renderPassDenoiserFilter).c_str(),
        AiNodeGetName(_driver));
    // We need NDC, and the easiest way is to use the position.
    const auto positionString = TfStringPrintf("P VECTOR %s %s", AiNodeGetName(_closestFilter), AiNodeGetName(_driver));
    const auto idString = TfStringPrintf("ID UINT %s %s", AiNodeGetName(_closestFilter), AiNodeGetName(_driver));
    AiArraySetStr(_outputsWithoutDenoiser, 0, beautyString.c_str());
    AiArraySetStr(_outputsWithoutDenoiser, 1, positionString.c_str());
    AiArraySetStr(_outputsWithoutDenoiser, 2, idString.c_str());

    AiArraySetStr(_outputsWithDenoiser, 0, beautyString.c_str());
    AiArraySetStr(_outputsWithDenoiser, 1, denoiserString.c_str());
    AiArraySetStr(_outputsWithDenoiser, 2, positionString.c_str());
    AiArraySetStr(_outputsWithDenoiser, 3, idString.c_str());

    _optixDenoiserInUse = _GetEnableOptixDenoiser();
    AiNodeSetArray(
        options, str::outputs, AiArrayCopy(_optixDenoiserInUse ? _outputsWithDenoiser : _outputsWithoutDenoiser));
    AiNodeSetBool(_driver, str::enable_optix_denoiser, _optixDenoiserInUse);

    const auto& config = HdArnoldConfig::GetInstance();
    AiNodeSetFlt(_camera, str::shutter_start, config.shutter_start);
    AiNodeSetFlt(_camera, str::shutter_end, config.shutter_end);
}

HdArnoldRenderPass::~HdArnoldRenderPass()
{
    AiNodeDestroy(_camera);
    AiNodeDestroy(_beautyFilter);
    AiNodeDestroy(_closestFilter);
    AiNodeDestroy(_driver);
    AiNodeDestroy(_denoiserFilter);
}

void HdArnoldRenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags)
{
    TF_UNUSED(renderTags);
    auto* renderParam = reinterpret_cast<HdArnoldRenderParam*>(_delegate->GetRenderParam());
    const auto vp = renderPassState->GetViewport();

    const auto projMtx = renderPassState->GetProjectionMatrix();
    const auto viewMtx = renderPassState->GetWorldToViewMatrix();
    if (projMtx != _projMtx || viewMtx != _viewMtx) {
        _projMtx = projMtx;
        _viewMtx = viewMtx;
        renderParam->Interrupt();
        AiNodeSetMatrix(_camera, str::matrix, HdArnoldConvertMatrix(_viewMtx.GetInverse()));
        AiNodeSetMatrix(_driver, HdArnoldDriver::projMtx, HdArnoldConvertMatrix(_projMtx));
        AiNodeSetMatrix(_driver, HdArnoldDriver::viewMtx, HdArnoldConvertMatrix(_viewMtx));
        const auto fov = static_cast<float>(GfRadiansToDegrees(atan(1.0 / _projMtx[0][0]) * 2.0));
        AiNodeSetFlt(_camera, str::fov, fov);
    }

    const auto enableOptixDenoiser = _GetEnableOptixDenoiser();
    if (enableOptixDenoiser != _optixDenoiserInUse) {
        _optixDenoiserInUse = enableOptixDenoiser;
        renderParam->Interrupt();
        if (_denoiserFilter == nullptr) {
            _denoiserFilter = AiNode(_delegate->GetUniverse(), str::denoise_optix_filter);
            AiNodeSetStr(_denoiserFilter, str::name, _delegate->GetLocalNodeName(str::renderPassDenoiserFilter));
        }
        auto* options = _delegate->GetOptions();
        AiNodeSetArray(
            options, str::outputs, AiArrayCopy(_optixDenoiserInUse ? _outputsWithDenoiser : _outputsWithoutDenoiser));
        AiNodeSetBool(_driver, str::enable_optix_denoiser, _optixDenoiserInUse);
    }

    const auto width = static_cast<int>(vp[2]);
    const auto height = static_cast<int>(vp[3]);
    const auto numPixels = static_cast<size_t>(width * height);
    if (width != _width || height != _height) {
        renderParam->Interrupt();
        hdArnoldEmptyBucketQueue([](const HdArnoldBucketData*) {});
        const auto oldNumPixels = static_cast<size_t>(_width * _height);
        _width = width;
        _height = height;

        auto* options = _delegate->GetOptions();
        AiNodeSetInt(options, str::xres, _width);
        AiNodeSetInt(options, str::yres, _height);

        if (oldNumPixels < numPixels) {
            _colorBuffer.resize(numPixels, AtRGBA8());
            _depthBuffer.resize(numPixels, 1.0f);
            _primIdBuffer.resize(numPixels, -1);
            memset(_colorBuffer.data(), 0, oldNumPixels * sizeof(AtRGBA8));
            std::fill(_depthBuffer.begin(), _depthBuffer.begin() + oldNumPixels, 1.0f);
            std::fill(_primIdBuffer.begin(), _primIdBuffer.begin() + oldNumPixels, -1);
        } else {
            if (numPixels != oldNumPixels) {
                _colorBuffer.resize(numPixels);
                _depthBuffer.resize(numPixels);
                _primIdBuffer.resize(numPixels);
            }
            memset(_colorBuffer.data(), 0, numPixels * sizeof(AtRGBA8));
            std::fill(_depthBuffer.begin(), _depthBuffer.end(), 1.0f);
            std::fill(_primIdBuffer.begin(), _primIdBuffer.end(), -1);
        }
    }

    _isConverged = renderParam->Render();
    bool needsUpdate = false;
    hdArnoldEmptyBucketQueue([this, &needsUpdate](const HdArnoldBucketData* data) {
        const auto xo = AiClamp(data->xo, 0, _width - 1);
        const auto xe = AiClamp(data->xo + data->sizeX, 0, _width - 1);
        if (xe == xo) {
            return;
        }
        const auto yo = AiClamp(data->yo, 0, _height - 1);
        const auto ye = AiClamp(data->yo + data->sizeY, 0, _height - 1);
        if (ye == yo) {
            return;
        }
        needsUpdate = true;
        const auto beautyWidth = (xe - xo) * sizeof(AtRGBA8);
        const auto depthWidth = (xe - xo) * sizeof(float);
        const auto inOffsetG = xo - data->xo - data->sizeX * data->yo;
        const auto outOffsetG = _width * (_height - 1);
        for (auto y = yo; y < ye; ++y) {
            const auto inOffset = data->sizeX * y + inOffsetG;
            const auto outOffset = xo + outOffsetG - _width * y;
            memcpy(_colorBuffer.data() + outOffset, data->beauty.data() + inOffset, beautyWidth);
            memcpy(_depthBuffer.data() + outOffset, data->depth.data() + inOffset, depthWidth);
            memcpy(_primIdBuffer.data() + outOffset, data->primId.data() + inOffset, depthWidth);
        }
    });

    HdRenderPassAovBindingVector aovBindings = renderPassState->GetAovBindings();

    // If the buffers are empty, needsUpdate will be false.
    if (aovBindings.empty()) {
        // No AOV bindings means blit current framebuffer contents.
#ifdef USD_HAS_FULLSCREEN_SHADER
        if (needsUpdate) {
            _fullscreenShader.SetTexture(
                _tokens->color, _width, _height, HdFormat::HdFormatUNorm8Vec4, _colorBuffer.data());
            _fullscreenShader.SetTexture(
                _tokens->depth, _width, _height, HdFormatFloat32, reinterpret_cast<uint8_t*>(_depthBuffer.data()));
        }
        _fullscreenShader.SetProgramToCompositor(true);
        _fullscreenShader.Draw();
#else
        if (needsUpdate) {
#ifdef USD_HAS_UPDATED_COMPOSITOR
            _compositor.UpdateColor(_width, _height, HdFormat::HdFormatUNorm8Vec4, _colorBuffer.data());
#else
            _compositor.UpdateColor(_width, _height, reinterpret_cast<uint8_t*>(_colorBuffer.data()));
#endif
            _compositor.UpdateDepth(_width, _height, reinterpret_cast<uint8_t*>(_depthBuffer.data()));
        }
        _compositor.Draw();
#endif
    } else {
        // Blit from the framebuffer to the currently selected AOVs.
        for (auto& aov : aovBindings) {
            if (!TF_VERIFY(aov.renderBuffer != nullptr)) {
                continue;
            }

            auto* rb = static_cast<HdArnoldRenderBuffer*>(aov.renderBuffer);
            // Forward convergence state to the render buffers...
            rb->SetConverged(_isConverged);

            if (needsUpdate) {
                if (aov.aovName == HdAovTokens->color) {
                    rb->Blit(
                        HdFormatUNorm8Vec4, width, height, 0, width, reinterpret_cast<uint8_t*>(_colorBuffer.data()));
                } else if (aov.aovName == HdAovTokens->depth) {
                    rb->Blit(HdFormatFloat32, width, height, 0, width, reinterpret_cast<uint8_t*>(_depthBuffer.data()));
                } else if (aov.aovName == HdAovTokens->primId) {
                    rb->Blit(HdFormatInt32, width, height, 0, width, reinterpret_cast<uint8_t*>(_primIdBuffer.data()));
                }
            }
        }
    }
}

bool HdArnoldRenderPass::_GetEnableOptixDenoiser() { return _gpuSupportEnabled && _delegate->GetEnableOptixDenoiser(); }

PXR_NAMESPACE_CLOSE_SCOPE
