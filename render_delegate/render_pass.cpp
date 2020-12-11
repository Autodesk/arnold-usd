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
#include "render_pass.h"

#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/staticTokens.h>

#include <pxr/imaging/hd/renderPassState.h>

#include <algorithm>

#include "camera.h"
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
    (dataType)
    (raw)
    (lpe)
    (primvar)
    ((_bool, "bool"))
    ((_int, "int"))
    (int64)
    ((_float, "float"))
    ((_double, "double"))
    ((_string, "string"))
    (token)
    (asset)
    (half2) (float2) (double2)
    (int3) (half3) (float3) (double3)
    (point3f) (point3d) (normal3f) (normal3d) (vector3f) (vector3d)
    (color3f) (color3d)
    (color4f) (color4d)
    (texCoord2f) (texCoord3f)
    (int4) (half4) (float4) (double4)
    (quath) (quatf) (quatd)
    // Additional entries from "Format" on Render Var LOP
    (color2f)
    (half) (float16)
    (color2h) (color3h) (color4h)
    (u8) (uint8)
    (color2u8) (color3u8) (color4u8)
    (i8) (int8)
    (color2i8) (color3i8) (color4i8)
    (int2)
    (uint) (uint2) (uint3) (uint4)
);
// clang-format on

TF_DEFINE_ENV_SETTING(HDARNOLD_default_filter, "box_filter", "Default filter type for RenderVars.");
TF_DEFINE_ENV_SETTING(HDARNOLD_default_filter_attributes, "", "Default filter attributes for RenderVars.");

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

struct ArnoldAOVType {
    const char* outputString;
    const AtString writer;
    const AtString reader;

    ArnoldAOVType(const char* _outputString, const AtString& _writer, const AtString& _reader)
        : outputString(_outputString), writer(_writer), reader(_reader)
    {
    }
};

const ArnoldAOVType AOVTypeINT {"INT", str::aov_write_int, str::user_data_int};
const ArnoldAOVType AOVTypeFLOAT {"FLOAT", str::aov_write_float, str::user_data_float};
const ArnoldAOVType AOVTypeVECTOR {"VECTOR", str::aov_write_vector, str::user_data_rgb};
const ArnoldAOVType AOVTypeVECTOR2 {"VECTOR2", str::aov_write_vector, str::user_data_rgb};
const ArnoldAOVType AOVTypeRGB {"RGB", str::aov_write_rgb, str::user_data_rgb};
const ArnoldAOVType AOVTypeRGBA {"RGBA", str::aov_write_rgba, str::user_data_rgba};

// The rules here:
// - Anything with 4 components                                           -> RGBA
// - Anything with a single floating point component                      -> FLOAT
// - Anything with a single integer-like or boolean component             -> INT
// - Anything with 3 floating point components and "color" in the name    -> RGB
// - Anything with 3 floating point components but no "color" in the name -> VECTOR
// - Anything with 2 components                                           -> VECTOR2
const std::unordered_map<TfToken, const ArnoldAOVType*, TfToken::HashFunctor> ArnoldAOVTypeMap{{
    {_tokens->_bool, &AOVTypeINT},
    {_tokens->_int, &AOVTypeINT},
    {_tokens->int64, &AOVTypeINT},
    {_tokens->_float, &AOVTypeFLOAT},
    {_tokens->_double, &AOVTypeFLOAT},
    {_tokens->half2, &AOVTypeVECTOR2},
    {_tokens->float2, &AOVTypeVECTOR2},
    {_tokens->double2, &AOVTypeVECTOR2},
    {_tokens->int3, &AOVTypeVECTOR},
    {_tokens->half3, &AOVTypeVECTOR},
    {_tokens->float3, &AOVTypeVECTOR},
    {_tokens->double3, &AOVTypeVECTOR},
    {_tokens->point3f, &AOVTypeVECTOR},
    {_tokens->point3d, &AOVTypeVECTOR},
    {_tokens->normal3f, &AOVTypeVECTOR},
    {_tokens->normal3d, &AOVTypeVECTOR},
    {_tokens->vector3f, &AOVTypeVECTOR},
    {_tokens->vector3d, &AOVTypeVECTOR},
    {_tokens->color3f, &AOVTypeRGB},
    {_tokens->color3d, &AOVTypeRGB},
    {_tokens->color4f, &AOVTypeRGBA},
    {_tokens->color4d, &AOVTypeRGBA},
    {_tokens->texCoord2f, &AOVTypeVECTOR2},
    {_tokens->texCoord3f, &AOVTypeVECTOR},
    {_tokens->int4, &AOVTypeRGBA},
    {_tokens->half4, &AOVTypeRGBA},
    {_tokens->float4, &AOVTypeRGBA},
    {_tokens->double4, &AOVTypeRGBA},
    {_tokens->quath, &AOVTypeRGBA},
    {_tokens->quatf, &AOVTypeRGBA},
    {_tokens->quatd, &AOVTypeRGBA},
    {_tokens->color2f, &AOVTypeVECTOR2},
    {_tokens->half, &AOVTypeFLOAT},
    {_tokens->float16, &AOVTypeFLOAT},
    {_tokens->color2h, &AOVTypeVECTOR2},
    {_tokens->color3h, &AOVTypeVECTOR},
    {_tokens->color4h, &AOVTypeRGBA},
    {_tokens->u8, &AOVTypeINT},
    {_tokens->uint8, &AOVTypeINT},
    {_tokens->color2u8, &AOVTypeVECTOR2},
    {_tokens->color3u8, &AOVTypeVECTOR},
    {_tokens->color4u8, &AOVTypeRGBA},
    {_tokens->i8, &AOVTypeINT},
    {_tokens->int8, &AOVTypeINT},
    {_tokens->color2i8, &AOVTypeVECTOR2},
    {_tokens->color3i8, &AOVTypeVECTOR},
    {_tokens->color4i8, &AOVTypeRGBA},
    {_tokens->int2, &AOVTypeVECTOR2},
    {_tokens->uint, &AOVTypeINT},
    {_tokens->uint2, &AOVTypeVECTOR2},
    {_tokens->uint3, &AOVTypeVECTOR},
    {_tokens->uint4, &AOVTypeRGBA},
}};

// Using an unordered_map would be nicer.
const ArnoldAOVType& _GetArnoldAOVTypeFromTokenType(const TfToken& type)
{
    const auto iter = ArnoldAOVTypeMap.find(type);
    return iter == ArnoldAOVTypeMap.end() ? AOVTypeRGB : *iter->second;
}

const TfToken& _GetTokenFromRenderBufferType(const HdRenderBuffer* buffer)
{
    // Use a wide type to make sure all components are set.
    if (Ai_unlikely(buffer == nullptr)) {
        return _tokens->color4f;
    }
    const auto format = buffer->GetFormat();
    switch (format) {
        case HdFormatUNorm8:
            return _tokens->uint8;
        case HdFormatUNorm8Vec2:
            return _tokens->color2u8;
        case HdFormatUNorm8Vec3:
            return _tokens->color3u8;
        case HdFormatUNorm8Vec4:
            return _tokens->color4u8;
        case HdFormatSNorm8:
            return _tokens->int8;
        case HdFormatSNorm8Vec2:
            return _tokens->color2i8;
        case HdFormatSNorm8Vec3:
            return _tokens->color3i8;
        case HdFormatSNorm8Vec4:
            return _tokens->color4i8;
        case HdFormatFloat16:
            return _tokens->half;
        case HdFormatFloat16Vec2:
            return _tokens->half2;
        case HdFormatFloat16Vec3:
            return _tokens->half3;
        case HdFormatFloat16Vec4:
            return _tokens->half4;
        case HdFormatFloat32:
            return _tokens->_float;
        case HdFormatFloat32Vec2:
            return _tokens->float2;
        case HdFormatFloat32Vec3:
            // We prefer RGB aovs instead of AI_TYPE_VECTOR.
            return _tokens->color3f;
        case HdFormatFloat32Vec4:
            return _tokens->float4;
        case HdFormatInt32:
            return _tokens->_int;
        case HdFormatInt32Vec2:
            return _tokens->int2;
        case HdFormatInt32Vec3:
            return _tokens->int3;
        case HdFormatInt32Vec4:
            return _tokens->int4;
        default:
            return _tokens->color4f;
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
    const auto defaultFilter = TfGetEnvSetting(HDARNOLD_default_filter);
    const auto defaultFilterAttributes = TfGetEnvSetting(HDARNOLD_default_filter_attributes);
    _defaultFilter = AiNode(universe, defaultFilter.c_str());
    // In case the defaultFilter string is an invalid filter type.
    if (_defaultFilter == nullptr || AiNodeEntryGetType(AiNodeGetNodeEntry(_defaultFilter)) != AI_NODE_FILTER) {
        _defaultFilter = AiNode(universe, str::box_filter);
    }
    if (!defaultFilterAttributes.empty()) {
        AiNodeSetAttributes(_defaultFilter, defaultFilterAttributes.c_str());
    }
    AiNodeSetStr(_defaultFilter, str::name, _delegate->GetLocalNodeName(str::renderPassFilter));
    _closestFilter = AiNode(universe, str::closest_filter);
    AiNodeSetStr(_closestFilter, str::name, _delegate->GetLocalNodeName(str::renderPassClosestFilter));
    _mainDriver = AiNode(universe, str::HdArnoldDriverMain);
    AiNodeSetStr(_mainDriver, str::name, _delegate->GetLocalNodeName(str::renderPassMainDriver));

    // Even though we are not displaying the prim id buffer, we still need it to detect background pixels.
    // clang-format off
    _fallbackBuffers = {{HdAovTokens->color, {&_fallbackColor, {}}},
                        {HdAovTokens->depth, {&_fallbackDepth, {}}},
                        {HdAovTokens->primId, {&_fallbackPrimId, {}}}};
    // clang-format on
    _fallbackOutputs = AiArrayAllocate(3, 1, AI_TYPE_STRING);
    // Setting up the fallback outputs when no
    const auto beautyString =
        TfStringPrintf("RGBA RGBA %s %s", AiNodeGetName(_defaultFilter), AiNodeGetName(_mainDriver));
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
    AiNodeDestroy(_defaultFilter);
    AiNodeDestroy(_closestFilter);
    AiNodeDestroy(_mainDriver);
    // We are not assigning this array to anything, so needs to be manually destroyed.
    AiArrayDestroy(_fallbackOutputs);

    _ClearRenderBuffers();
}

void HdArnoldRenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags)
{
    TF_UNUSED(renderTags);
    auto* renderParam = reinterpret_cast<HdArnoldRenderParam*>(_delegate->GetRenderParam());
    const auto vp = renderPassState->GetViewport();

    const auto* currentUniverseCamera =
        static_cast<const AtNode*>(AiNodeGetPtr(AiUniverseGetOptions(_delegate->GetUniverse()), str::camera));
    const auto* camera = reinterpret_cast<const HdArnoldCamera*>(renderPassState->GetCamera());
    const auto useOwnedCamera = camera == nullptr;
    // If camera is nullptr from the render pass state, we are using a camera created by the renderpass.
    if (useOwnedCamera) {
        if (currentUniverseCamera != _camera) {
            renderParam->Interrupt();
            AiNodeSetPtr(AiUniverseGetOptions(_delegate->GetUniverse()), str::camera, _camera);
        }
    } else {
        if (currentUniverseCamera != camera->GetCamera()) {
            renderParam->Interrupt();
            AiNodeSetPtr(AiUniverseGetOptions(_delegate->GetUniverse()), str::camera, camera->GetCamera());
        }
    }

    const auto projMtx = renderPassState->GetProjectionMatrix();
    const auto viewMtx = renderPassState->GetWorldToViewMatrix();
    if (projMtx != _projMtx || viewMtx != _viewMtx) {
        _projMtx = projMtx;
        _viewMtx = viewMtx;
        renderParam->Interrupt(false);
        AiNodeSetMatrix(_mainDriver, str::projMtx, HdArnoldConvertMatrix(_projMtx));
        AiNodeSetMatrix(_mainDriver, str::viewMtx, HdArnoldConvertMatrix(_viewMtx));
        if (useOwnedCamera) {
            const auto fov = static_cast<float>(GfRadiansToDegrees(atan(1.0 / _projMtx[0][0]) * 2.0));
            AiNodeSetFlt(_camera, str::fov, fov);
            AiNodeSetMatrix(_camera, str::matrix, HdArnoldConvertMatrix(_viewMtx.GetInverse()));
        }
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

#ifdef USD_DO_NOT_BLIT
    TF_VERIFY(!aovBindings.empty(), "No AOV bindings to render into!");
#endif

#ifndef USD_DO_NOT_BLIT
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
            AiNodeSetPtr(_mainDriver, str::color_pointer, &_fallbackColor);
            AiNodeSetPtr(_mainDriver, str::depth_pointer, &_fallbackDepth);
            AiNodeSetPtr(_mainDriver, str::id_pointer, &_fallbackPrimId);
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
#endif
        // AOV bindings exists, so first we are checking if anything has changed.
        // If something has changed, then we rebuild the local storage class, and the outputs definition.
        // We expect Hydra to resize the render buffers.
        if (_RenderBuffersChanged(aovBindings) || _usingFallbackBuffers) {
            _usingFallbackBuffers = false;
            renderParam->Interrupt();
            _ClearRenderBuffers();
            AiNodeSetPtr(_mainDriver, str::color_pointer, nullptr);
            AiNodeSetPtr(_mainDriver, str::depth_pointer, nullptr);
            AiNodeSetPtr(_mainDriver, str::id_pointer, nullptr);
            // Rebuilding render buffers
            const auto numBindings = static_cast<unsigned int>(aovBindings.size());
            auto* outputsArray = AiArrayAllocate(numBindings, 1, AI_TYPE_STRING);
            auto* outputs = static_cast<AtString*>(AiArrayMap(outputsArray));
            std::vector<AtString> lightPathExpressions;
            std::vector<AtNode*> aovShaders;
            // When creating the outputs array we follow this logic:
            // - color -> RGBA RGBA for the beauty box filter by default
            // - depth -> P VECTOR for remapping point to depth using the projection matrices closest filter by default
            // - primId -> ID UINT closest filter by default
            // - everything else -> aovName RGB closest filter by default
            // We are using box filter for the color and closest for everything else.
            const auto* boxName = AiNodeGetName(_defaultFilter);
            const auto* closestName = AiNodeGetName(_closestFilter);
            const auto* mainDriverName = AiNodeGetName(_mainDriver);
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
                const auto sourceName = _GetOptionalSetting<std::string>(
                    binding.aovSettings, _tokens->sourceName, binding.aovName.GetString());
                // When using a raw buffer, we have special behavior for color, depth and ID. Otherwise we are creating
                // an aov with the same name. We can't just check for the source name; for example: using a primvar
                // type and displaying a "color" or a "depth" user data is a valid use case.
                const auto isRaw = sourceType == _tokens->raw;
                if (isRaw && sourceName == HdAovTokens->color) {
                    *outputs = AtString(
                        TfStringPrintf("RGBA RGBA %s %s", filterName != nullptr ? filterName : boxName, mainDriverName)
                            .c_str());
                    AiNodeSetPtr(_mainDriver, str::color_pointer, binding.renderBuffer);
                } else if (isRaw && sourceName == HdAovTokens->depth) {
                    *outputs =
                        AtString(TfStringPrintf(
                                     "P VECTOR %s %s", filterName != nullptr ? filterName : closestName, mainDriverName)
                                     .c_str());
                    AiNodeSetPtr(_mainDriver, str::depth_pointer, binding.renderBuffer);
                } else if (isRaw && sourceName == HdAovTokens->primId) {
                    *outputs =
                        AtString(TfStringPrintf(
                                     "ID UINT %s %s", filterName != nullptr ? filterName : closestName, mainDriverName)
                                     .c_str());
                    AiNodeSetPtr(_mainDriver, str::id_pointer, binding.renderBuffer);
                } else {
                    // Querying the data format from USD, with a default value of color3f.
                    const auto format = _GetOptionalSetting<TfToken>(
                        binding.aovSettings, _tokens->dataType, _GetTokenFromRenderBufferType(buffer.buffer));
                    // Creating a separate driver for each aov.
                    buffer.driver = AiNode(_delegate->GetUniverse(), str::HdArnoldDriverAOV);
                    const auto driverNameStr = _delegate->GetLocalNodeName(
                        AtString{TfStringPrintf("HdArnoldRenderPass_aov_driver_%p", buffer.driver).c_str()});
                    AiNodeSetStr(buffer.driver, str::name, driverNameStr);
                    AiNodeSetPtr(buffer.driver, str::aov_pointer, buffer.buffer);
                    const char* aovName = nullptr;
                    const auto arnoldTypes = _GetArnoldAOVTypeFromTokenType(format);
                    if (sourceType == _tokens->lpe) {
                        aovName = binding.aovName.GetText();
                        // We have to add the light path expression to the outputs node in the format of:
                        // "aov_name lpe" like "beauty C.*"
                        lightPathExpressions.emplace_back(
                            TfStringPrintf("%s %s", binding.aovName.GetText(), sourceName.c_str()).c_str());
                    } else if (sourceType == _tokens->primvar) {
                        aovName = binding.aovName.GetText();
                        // We need to add a aov write shader to the list of aov_shaders on the options node. Each
                        // of this shader will be executed on every surface.
                        buffer.writer = AiNode(_delegate->GetUniverse(), arnoldTypes.writer);
                        if (sourceName == "st" || sourceName == "uv") { // st and uv are written to the built-in UV
                            buffer.reader = AiNode(_delegate->GetUniverse(), str::utility);
                            AiNodeSetStr(buffer.reader, str::color_mode, str::uv);
                            AiNodeSetStr(buffer.reader, str::shade_mode, str::flat);
                        } else {
                            buffer.reader = AiNode(_delegate->GetUniverse(), arnoldTypes.reader);
                            AiNodeSetStr(buffer.reader, str::attribute, sourceName.c_str());
                        }
                        const auto writerName = _delegate->GetLocalNodeName(
                            AtString{TfStringPrintf("HdArnoldRenderPass_aov_writer_%p", buffer.writer).c_str()});
                        const auto readerName = _delegate->GetLocalNodeName(
                            AtString{TfStringPrintf("HdArnoldRenderPass_aov_reader_%p", buffer.reader).c_str()});
                        AiNodeSetStr(buffer.writer, str::name, writerName);
                        AiNodeSetStr(buffer.reader, str::name, readerName);
                        AiNodeSetStr(buffer.writer, str::aov_name, aovName);
                        AiNodeSetBool(buffer.writer, str::blend_opacity, false);
                        AiNodeLink(buffer.reader, str::aov_input, buffer.writer);
                        aovShaders.push_back(buffer.writer);
                    } else {
                        aovName = sourceName.c_str();
                    }
                    *outputs = AtString(TfStringPrintf(
                                            "%s %s %s %s", aovName, arnoldTypes.outputString,
                                            filterName != nullptr ? filterName : boxName, AiNodeGetName(buffer.driver))
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
            AiNodeSetArray(
                _delegate->GetOptions(), str::aov_shaders,
                aovShaders.empty()
                    ? AiArray(0, 1, AI_TYPE_NODE)
                    : AiArrayConvert(static_cast<uint32_t>(aovShaders.size()), 1, AI_TYPE_NODE, aovShaders.data()));
            clearBuffers(_renderBuffers);
        }
#ifndef USD_DO_NOT_BLIT
    }
#endif

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
    }
#ifndef USD_DO_NOT_BLIT
    else {
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
            _compositor.UpdateColor(_width, _height, HdFormat::HdFormatFloat32Vec4, color);
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
#endif
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
        if (buffer.second.driver != nullptr) {
            AiNodeDestroy(buffer.second.driver);
        }
        if (buffer.second.writer != nullptr) {
            AiNodeDestroy(buffer.second.writer);
        }
        if (buffer.second.reader != nullptr) {
            AiNodeDestroy(buffer.second.reader);
        }
    }
    decltype(_renderBuffers){}.swap(_renderBuffers);
}

PXR_NAMESPACE_CLOSE_SCOPE
