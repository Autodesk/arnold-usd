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

#include "config.h"
#include "constant_strings.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (color)
    (depth)
    ((aovParameters, ""))
);
// clang-format on

HdArnoldRenderPass::HdArnoldRenderPass(
    HdArnoldRenderDelegate* delegate, HdRenderIndex* index, const HdRprimCollection& collection)
    : HdRenderPass(index, collection),
      _delegate(delegate),
      _fallbackColor(SdfPath::EmptyPath()),
      _fallbackDepth(SdfPath::EmptyPath()),
      _fallbackPrimId(SdfPath::EmptyPath())
{
    auto* universe = _delegate->GetUniverse();
    _camera = AiNode(universe, str::persp_camera);
    AiNodeSetPtr(AiUniverseGetOptions(universe), str::camera, _camera);
    AiNodeSetStr(_camera, str::name, _delegate->GetLocalNodeName(str::renderPassCamera));
    _beautyFilter = AiNode(universe, str::box_filter);
    AiNodeSetStr(_beautyFilter, str::name, _delegate->GetLocalNodeName(str::renderPassFilter));
    _closestFilter = AiNode(universe, str::closest_filter);
    AiNodeSetStr(_closestFilter, str::name, _delegate->GetLocalNodeName(str::renderPassClosestFilter));
    _driver = AiNode(universe, str::HdArnoldDriver);
    AiNodeSetStr(_driver, str::name, _delegate->GetLocalNodeName(str::renderPassDriver));
    AiNodeSetPtr(_driver, str::aov_pointer, &_renderBuffers);

    // Even though we are not displaying the prim id buffer, we still need it to detect background pixels.
    _fallbackBuffers = {{HdAovTokens->color, {&_fallbackColor, {}}},
                        {HdAovTokens->depth, {&_fallbackDepth, {}}},
                        {HdAovTokens->primId, {&_fallbackPrimId, {}}}};
    _fallbackOutputs = AiArrayAllocate(3, 1, AI_TYPE_STRING);
    // Setting up the fallback outputs when no
    const auto beautyString = TfStringPrintf("RGBA RGBA %s %s", AiNodeGetName(_beautyFilter), AiNodeGetName(_driver));
    const auto positionString = TfStringPrintf("P VECTOR %s %s", AiNodeGetName(_closestFilter), AiNodeGetName(_driver));
    const auto idString = TfStringPrintf("ID UINT %s %s", AiNodeGetName(_closestFilter), AiNodeGetName(_driver));
    AiArraySetStr(_fallbackOutputs, 0, beautyString.c_str());
    AiArraySetStr(_fallbackOutputs, 1, positionString.c_str());
    AiArraySetStr(_fallbackOutputs, 2, idString.c_str());

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
    // We are not assigning this array to anything, so needs to be manually destroyed.
    AiArrayDestroy(_fallbackOutputs);
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
        AiNodeSetMatrix(_driver, str::projMtx, HdArnoldConvertMatrix(_projMtx));
        AiNodeSetMatrix(_driver, str::viewMtx, HdArnoldConvertMatrix(_viewMtx));
        const auto fov = static_cast<float>(GfRadiansToDegrees(atan(1.0 / _projMtx[0][0]) * 2.0));
        AiNodeSetFlt(_camera, str::fov, fov);
    }

    const auto width = static_cast<int>(vp[2]);
    const auto height = static_cast<int>(vp[3]);
    if (width != _width || height != _height) {
        renderParam->Interrupt();
        _width = width;
        _height = height;
        auto* options = _delegate->GetOptions();
        AiNodeSetInt(options, str::xres, _width);
        AiNodeSetInt(options, str::yres, _height);
    }

    // We are checking if the current aov bindings match the ones we already created, if not,
    // then rebuild the driver setup.
    // If AOV bindings are empty, we are only setting up color and depth for basic opengl composition. This should
    // not happen often.
    // TODO(pal): Remove bindings to P and RGBA. Those are used for other buffers. Or add support for writing to
    //  these in the driver.
    HdRenderPassAovBindingVector aovBindings = renderPassState->GetAovBindings();
    // These buffers are not supported, but we still need to allocate and set them up for hydra.
    aovBindings.erase(
        std::remove_if(
            aovBindings.begin(), aovBindings.end(),
            [](const HdRenderPassAovBinding& binding) -> bool {
                return binding.aovName == HdAovTokens->elementId || binding.aovName == HdAovTokens->instanceId ||
                       binding.aovName == HdAovTokens->pointId;
            }),
        aovBindings.end());

    if (aovBindings.empty()) {
        // TODO (pal): Implement.
        // We are first checking if the right storage pointer is set on the driver.
        // If not, then we need to reset the aov setup and set the outputs definition on the driver.
        // If it's the same pointer, we still need to check the dimensions, if they don't match the global dimensions,
        // then reallocate those render buffers.
        // If USD has the newer compositor class, we can allocate float buffers for the color, otherwise we need to
        // stick to UNorm8.
        if (!_usingFallbackBuffers) {
            renderParam->Interrupt();
            AiNodeSetArray(_delegate->GetOptions(), str::outputs, AiArrayCopy(_fallbackOutputs));
            _usingFallbackBuffers = true;
            AiNodeSetPtr(_driver, str::aov_pointer, &_fallbackBuffers);
        }
        if (_fallbackColor.GetWidth() != _width || _fallbackColor.GetHeight() != _height) {
            renderParam->Interrupt();
#ifdef USD_HAS_UPDATED_COMPOSITOR
            _fallbackColor.Allocate({_width, _height, 1}, HdFormatFloat32Vec4, false);
#else
            _fallbackColor.Allocate({_width, _height, 1}, HdFormatUNorm8Vec4, false);
#endif
            _fallbackDepth.Allocate({_width, _height, 1}, HdFormatFloat32, false);
            _fallbackPrimId.Allocate({_width, _height, 1}, HdFormatInt32, false);
        }
    } else {
        // AOV bindings exists, so first we are checking if anything has changed.
        // If something has changed, then we rebuild the local storage class, and the outputs definition.
        // We expect Hydra to resize the render buffers.
        if (_RenderBuffersChanged(aovBindings) || _usingFallbackBuffers) {
            _usingFallbackBuffers = false;
            renderParam->Interrupt();
            _renderBuffers.clear();
            // Rebuilding render buffers
            const auto numBindings = static_cast<unsigned int>(aovBindings.size());
            auto* outputsArray = AiArrayAllocate(numBindings, 1, AI_TYPE_STRING);
            auto* outputs = static_cast<AtString*>(AiArrayMap(outputsArray));
            // When creating the outputs array we follow this logic:
            // - color -> RGBA RGBA for the beauty + box filter
            // - depth -> P VECTOR for remapping point to depth using the projection matrices + closest filter
            // - primId -> ID UINT + closest filter
            // - everything else -> aovName RGB + closest filter
            // We are using box filter for the color and closest for everything else.
            const auto* boxName = AiNodeGetName(_beautyFilter);
            const auto* closestName = AiNodeGetName(_closestFilter);
            const auto* driverName = AiNodeGetName(_driver);
            for (const auto& binding : aovBindings) {
                if (binding.aovName == HdAovTokens->color) {
                    *outputs = AtString(TfStringPrintf("RGBA RGBA %s %s", boxName, driverName).c_str());
                } else if (binding.aovName == HdAovTokens->depth) {
                    *outputs = AtString(TfStringPrintf("P VECTOR %s %s", closestName, driverName).c_str());
                } else if (binding.aovName == HdAovTokens->primId) {
                    *outputs = AtString(TfStringPrintf("ID UINT %s %s", closestName, driverName).c_str());
                } else {
                    *outputs = AtString(
                        TfStringPrintf("%s RGB %s %s", binding.aovName.GetText(), closestName, driverName).c_str());
                }
                // Sadly we only get a raw pointer here, so we have to expect hydra not clearing up render buffers
                // while they are being used.
                _renderBuffers[binding.aovName].buffer = dynamic_cast<HdArnoldRenderBuffer*>(binding.renderBuffer);
                // TODO(pal): Setup filtering information if available.
                _renderBuffers[binding.aovName].settings = binding.aovSettings;
                outputs += 1;
            }
            AiArrayUnmap(outputsArray);
            AiNodeSetArray(_delegate->GetOptions(), str::outputs, outputsArray);
        }
    }

    const auto renderStatus = renderParam->Render();
    _isConverged = renderStatus != HdArnoldRenderParam::Status::Converging;

    auto clearBuffers = [&](HdArnoldRenderBufferStorage& storage) {
        static std::vector<uint8_t> zeroData;
        zeroData.resize(_width * _height * 4);
        for (auto& buffer : storage) {
            if (buffer.second.buffer != nullptr) {
                buffer.second.buffer->WriteBucket(0, 0, _width, _height, HdFormatUNorm8Vec4, zeroData.data());
            }
        }
    };

    // We need to set the converged status of the render buffers.
    if (!aovBindings.empty()) {
        // Clearing all AOVs if render was aborted.
        if (renderStatus == HdArnoldRenderParam::Status::Aborted) {
            clearBuffers(_renderBuffers);
        }
        for (auto& buffer : _renderBuffers) {
            if (buffer.second.buffer != nullptr) {
                buffer.second.buffer->SetConverged(_isConverged);
            }
        }
        // If the buffers are empty, we have to blit the data from the fallback buffers to OpenGL.
    } else {
        // Clearing all AOVs if render was aborted.
        if (renderStatus == HdArnoldRenderParam::Status::Aborted) {
            clearBuffers(_fallbackBuffers);
        }
// No AOV bindings means blit current framebuffer contents.
// TODO(pal): Only update the compositor and the fullscreen shader if something has changed.
// When using fallback buffers, it's enough to check if the color has any updates.
#ifdef USD_HAS_FULLSCREEN_SHADER
        if (_fallbackColor.HasUpdates()) {
            auto* color = _fallbackColor.Map();
            auto* depth = _fallbackDepth.Map();
            _fullscreenShader.SetTexture(
                _tokens->color, _width, _height,
#ifdef USD_HAS_UPDATED_COMPOSITOR
                HdFormat::HdFormatFloat32Vec4,
#else
                HdFormat::HdFormatUNorm8Vec4,
#endif
                color);
            _fullscreenShader.SetTexture(
                _tokens->depth, _width, _height, HdFormatFloat32, reinterpret_cast<uint8_t*>(depth));
            _fallbackColor.Unmap();
            _fallbackDepth.Unmap();
        }
        _fullscreenShader.SetProgramToCompositor(true);
        _fullscreenShader.Draw();
#else
        if (_fallbackColor.HasUpdates()) {
            auto* color = _fallbackColor.Map();
            auto* depth = _fallbackDepth.Map();
#ifdef USD_HAS_UPDATED_COMPOSITOR
            _compositor.UpdateColor(_width, _height, HdFormat::HdFormatUNorm8Vec4, color);
#else
            _compositor.UpdateColor(_width, _height, reinterpret_cast<uint8_t*>(color));
#endif
            _compositor.UpdateDepth(_width, _height, reinterpret_cast<uint8_t*>(depth));
            _fallbackColor.Unmap();
            _fallbackDepth.Unmap();
        }
        _compositor.Draw();
#endif
    }
}

bool HdArnoldRenderPass::_RenderBuffersChanged(const HdRenderPassAovBindingVector& aovBindings)
{
    if (aovBindings.size() != _renderBuffers.size()) {
        return true;
    }
    for (const auto& binding : aovBindings) {
        const auto it = _renderBuffers.find(binding.aovName);
        if (it == _renderBuffers.end() || it->second.settings != binding.aovSettings) {
            return true;
        }
    }

    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
