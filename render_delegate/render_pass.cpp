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
    ((aovSetting, "arnold:"))
    ((aovSettingFilter, "arnold:filter"))
    ((aovSettingFormat, "driver:parameters:aov:format"))
    (sourceName)
    (sourceType)
    (raw)
    (lpe)
    ((_float, "float"))
    ((_int, "int"))
    (i8) (int8)
    (ui8) (uint8)
    (half) (float16)
    (float2) (float3) (float4)
    (half2) (half3) (half4)
    (color2f) (color3f) (color4f)
    (color2h) (color3h) (color4h)
    (color2u8) (color3u8) (color4u8)
    (color2i8) (color3i8) (color4i8)
    (int2) (int3) (int4)
    (uint2) (uint3) (uint4)
);
// clang-format on

namespace {

inline std::string _GetStringFromValue(const VtValue& value, const std::string defaultValue = "")
{
    if (value.IsHolding<std::string>()) {
        return value.UncheckedGet<std::string>();
    } else if (value.IsHolding<TfToken>()) {
        return value.UncheckedGet<TfToken>().GetString();
    } else {
        return defaultValue;
    }
}

template <typename T>
T _GetOptionalSetting(
    const decltype(HdRenderPassAovBinding::aovSettings)& settings, const TfToken& settingName, const T& defaultValue)
{
    const auto it = settings.find(settingName);
    if (it == settings.end()) {
        return defaultValue;
    }
    return it->second.IsHolding<T>() ? it->second.UncheckedGet<T>() : defaultValue;
}

const char* _GetArnoldTypeFromTokenType(const TfToken& type)
{
    // We check for the most common cases first.
    if (type == _tokens->color3f) {
        return "RGB";
    } else if (type == _tokens->color4f) {
        return "RGBA";
    } else if (type == _tokens->float3) {
        return "VECTOR";
    } else if (type == _tokens->float2) {
        return "VECTOR2";
    } else if (type == _tokens->_float) {
        return "FLOAT";
    } else if (type == _tokens->_int) {
        return "INT";
    } else if (type == _tokens->i8 || type == _tokens->uint8) {
        return "INT";
    } else if (type == _tokens->half || type == _tokens->float16) {
        return "FLOAT";
    } else if (
        type == _tokens->half2 || type == _tokens->color2f || type == _tokens->color2h || type == _tokens->color2u8 ||
        type == _tokens->color2i8 || type == _tokens->int2 || type == _tokens->uint2) {
        return "VECTOR2";
    } else if (type == _tokens->half3 || type == _tokens->int3 || type == _tokens->uint3) {
        return "VECTOR";
    } else if (
        type == _tokens->float4 || type == _tokens->half4 || type == _tokens->color4f || type == _tokens->color4h ||
        type == _tokens->color4u8 || type == _tokens->color4i8 || type == _tokens->int4 || type == _tokens->uint4) {
        return "RGBA";
    } else {
        return "RGB";
    }
}

} // namespace

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
    _mainDriver = AiNode(universe, str::HdArnoldDriverMain);
    AiNodeSetStr(_mainDriver, str::name, _delegate->GetLocalNodeName(str::renderPassMainDriver));
    AiNodeSetPtr(_mainDriver, str::aov_pointer, &_renderBuffers);
    _aovDriver = AiNode(universe, str::HdArnoldDriverAOV);
    AiNodeSetStr(_aovDriver, str::name, _delegate->GetLocalNodeName(str::renderPassAOVDriver));
    AiNodeSetPtr(_aovDriver, str::aov_pointer, &_renderBuffers);

    // Even though we are not displaying the prim id buffer, we still need it to detect background pixels.
    _fallbackBuffers = {{HdAovTokens->color, {&_fallbackColor, {}}},
                        {HdAovTokens->depth, {&_fallbackDepth, {}}},
                        {HdAovTokens->primId, {&_fallbackPrimId, {}}}};
    _fallbackOutputs = AiArrayAllocate(3, 1, AI_TYPE_STRING);
    // Setting up the fallback outputs when no
    const auto beautyString =
        TfStringPrintf("RGBA RGBA %s %s", AiNodeGetName(_beautyFilter), AiNodeGetName(_mainDriver));
    const auto positionString =
        TfStringPrintf("P VECTOR %s %s", AiNodeGetName(_closestFilter), AiNodeGetName(_mainDriver));
    const auto idString = TfStringPrintf("ID UINT %s %s", AiNodeGetName(_closestFilter), AiNodeGetName(_mainDriver));
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
    AiNodeDestroy(_mainDriver);
    AiNodeDestroy(_aovDriver);
    // We are not assigning this array to anything, so needs to be manually destroyed.
    AiArrayDestroy(_fallbackOutputs);

    _ClearRenderBuffers();
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
        renderParam->Interrupt(false);
        AiNodeSetMatrix(_camera, str::matrix, HdArnoldConvertMatrix(_viewMtx.GetInverse()));
        AiNodeSetMatrix(_mainDriver, str::projMtx, HdArnoldConvertMatrix(_projMtx));
        AiNodeSetMatrix(_mainDriver, str::viewMtx, HdArnoldConvertMatrix(_viewMtx));
        const auto fov = static_cast<float>(GfRadiansToDegrees(atan(1.0 / _projMtx[0][0]) * 2.0));
        AiNodeSetFlt(_camera, str::fov, fov);
    }

    const auto width = static_cast<int>(vp[2]);
    const auto height = static_cast<int>(vp[3]);
    if (width != _width || height != _height) {
        renderParam->Interrupt(false);
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
                if (binding.aovName == HdAovTokens->elementId || binding.aovName == HdAovTokens->instanceId ||
                    binding.aovName == HdAovTokens->pointId) {
                    // Set these buffers to converged, as we never write any data.
                    if (binding.renderBuffer != nullptr && !binding.renderBuffer->IsConverged()) {
                        auto* renderBuffer = dynamic_cast<HdArnoldRenderBuffer*>(binding.renderBuffer);
                        if (Ai_likely(renderBuffer != nullptr)) {
                            renderBuffer->SetConverged(true);
                        }
                    }
                    return true;
                } else {
                    return false;
                }
            }),
        aovBindings.end());

    auto clearBuffers = [&](HdArnoldRenderBufferStorage& storage) {
        static std::vector<uint8_t> zeroData;
        zeroData.resize(_width * _height * 4);
        for (auto& buffer : storage) {
            if (buffer.second.buffer != nullptr) {
                buffer.second.buffer->WriteBucket(0, 0, _width, _height, HdFormatUNorm8Vec4, zeroData.data());
            }
        }
    };

    if (aovBindings.empty()) {
        // We are first checking if the right storage pointer is set on the driver.
        // If not, then we need to reset the aov setup and set the outputs definition on the driver.
        // If it's the same pointer, we still need to check the dimensions, if they don't match the global dimensions,
        // then reallocate those render buffers.
        // If USD has the newer compositor class, we can allocate float buffers for the color, otherwise we need to
        // stick to UNorm8.
        if (!_usingFallbackBuffers) {
            renderParam->Interrupt(false);
            AiNodeSetArray(_delegate->GetOptions(), str::outputs, AiArrayCopy(_fallbackOutputs));
            _usingFallbackBuffers = true;
            AiNodeSetPtr(_mainDriver, str::aov_pointer, &_fallbackBuffers);
        }
        if (_fallbackColor.GetWidth() != _width || _fallbackColor.GetHeight() != _height) {
            renderParam->Interrupt(false);
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
            _ClearRenderBuffers();
            // Rebuilding render buffers
            const auto numBindings = static_cast<unsigned int>(aovBindings.size());
            auto* outputsArray = AiArrayAllocate(numBindings, 1, AI_TYPE_STRING);
            auto* outputs = static_cast<AtString*>(AiArrayMap(outputsArray));
            std::vector<AtString> lightPathExpressions;
            // When creating the outputs array we follow this logic:
            // - color -> RGBA RGBA for the beauty box filter by default
            // - depth -> P VECTOR for remapping point to depth using the projection matrices closest filter by default
            // - primId -> ID UINT closest filter by default
            // - everything else -> aovName RGB closest filter by default
            // We are using box filter for the color and closest for everything else.
            const auto* boxName = AiNodeGetName(_beautyFilter);
            const auto* closestName = AiNodeGetName(_closestFilter);
            const auto* mainDriverName = AiNodeGetName(_mainDriver);
            const auto* aovDriverName = AiNodeGetName(_aovDriver);
            for (const auto& binding : aovBindings) {
                auto& buffer = _renderBuffers[binding.aovName];
                // Sadly we only get a raw pointer here, so we have to expect hydra not clearing up render buffers
                // while they are being used.
                buffer.buffer = dynamic_cast<HdArnoldRenderBuffer*>(binding.renderBuffer);
                buffer.settings = binding.aovSettings;
                // We first check if the filterNode exists.
                const char* filterName = [&]() -> const char* {
                    // We need to make sure that it's holding a string, then try to create it to make sure
                    // it's a node type supported by Arnold.
                    const auto filterType =
                        _GetOptionalSetting(binding.aovSettings, _tokens->aovSettingFilter, std::string{});
                    if (filterType.empty()) {
                        return nullptr;
                    }
                    buffer.filter = AiNode(_delegate->GetUniverse(), filterType.c_str());
                    if (buffer.filter == nullptr) {
                        return nullptr;
                    }
                    const auto filterNameStr = _delegate->GetLocalNodeName(
                        AtString{TfStringPrintf("HdArnoldRenderPass_filter_%p", buffer.filter).c_str()});
                    AiNodeSetStr(buffer.filter, str::name, filterNameStr);
                    const auto* nodeEntry = AiNodeGetNodeEntry(buffer.filter);
                    // We are first checking for the filter parameters prefixed with "arnold:", then doing a second
                    // loop to check for "arnold:filter_type:" prefixed parameters. The reason for two loops is
                    // we want the second version to overwrite the first one, and with unordered_map, we are not
                    // getting any sort of ordering.
                    auto readFilterParameters = [&](const TfToken& filterPrefix) {
                        for (const auto& setting : binding.aovSettings) {
                            // We already processed the filter parameter
                            if (setting.first != _tokens->aovSettingFilter &&
                                TfStringStartsWith(setting.first, filterPrefix)) {
                                const AtString parameterName(setting.first.GetText() + filterPrefix.size());
                                // name is special in arnold
                                if (parameterName == str::name) {
                                    continue;
                                }
                                const auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, parameterName);
                                if (paramEntry != nullptr) {
                                    HdArnoldSetParameter(buffer.filter, paramEntry, setting.second);
                                }
                            }
                        }
                    };

                    readFilterParameters(_tokens->aovSetting);
                    readFilterParameters(
                        TfToken{TfStringPrintf("%s%s:", _tokens->aovSetting.GetText(), filterType.c_str())});

                    return AiNodeGetName(buffer.filter);
                }();
                const auto sourceType =
                    _GetOptionalSetting<TfToken>(binding.aovSettings, _tokens->sourceType, _tokens->raw);
                const auto sourceName =
                    _GetOptionalSetting<std::string>(binding.aovSettings, _tokens->sourceName, "color");
                if (binding.aovName == HdAovTokens->color) {
                    *outputs = AtString(
                        TfStringPrintf("RGBA RGBA %s %s", filterName != nullptr ? filterName : boxName, mainDriverName)
                            .c_str());
                } else if (binding.aovName == HdAovTokens->depth) {
                    *outputs =
                        AtString(TfStringPrintf(
                                     "P VECTOR %s %s", filterName != nullptr ? filterName : closestName, mainDriverName)
                                     .c_str());
                } else if (binding.aovName == HdAovTokens->primId) {
                    *outputs =
                        AtString(TfStringPrintf(
                                     "ID UINT %s %s", filterName != nullptr ? filterName : closestName, mainDriverName)
                                     .c_str());
                } else {
                    if (sourceType == _tokens->lpe) {
                        lightPathExpressions.emplace_back(
                            TfStringPrintf("%s %s", binding.aovName.GetText(), sourceName.c_str()).c_str());
                    }
                    // Houdini specific
                    const auto format =
                        _GetOptionalSetting<TfToken>(binding.aovSettings, _tokens->aovSettingFormat, _tokens->color3f);
                    *outputs =
                        AtString(TfStringPrintf(
                                     "%s %s %s %s", binding.aovName.GetText(), _GetArnoldTypeFromTokenType(format),
                                     filterName != nullptr ? filterName : closestName, aovDriverName)
                                     .c_str());
                }
                outputs += 1;
            }
            AiArrayUnmap(outputsArray);
            AiNodeSetArray(_delegate->GetOptions(), str::outputs, outputsArray);
            AiNodeSetArray(
                _delegate->GetOptions(), str::light_path_expressions,
                lightPathExpressions.empty() ? AiArray(0, 1, AI_TYPE_STRING)
                                             : AiArrayConvert(
                                                   static_cast<uint32_t>(lightPathExpressions.size()), 1,
                                                   AI_TYPE_STRING, lightPathExpressions.data()));
            clearBuffers(_renderBuffers);
        }
    }

    const auto renderStatus = renderParam->Render();
    _isConverged = renderStatus != HdArnoldRenderParam::Status::Converging;

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
        //  When using fallback buffers, it's enough to check if the color has any updates.
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

void HdArnoldRenderPass::_ClearRenderBuffers()
{
    for (auto& buffer : _renderBuffers) {
        if (buffer.second.filter != nullptr) {
            AiNodeDestroy(buffer.second.filter);
        }
    }
    decltype(_renderBuffers){}.swap(_renderBuffers);
}

PXR_NAMESPACE_CLOSE_SCOPE
