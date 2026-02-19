//
// SPDX-License-Identifier: Apache-2.0
//

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
// Modifications Copyright 2022 Autodesk, Inc.
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

#include <pxr/base/gf/rect2i.h>

#include <pxr/imaging/hd/renderPassState.h>

#include <algorithm>

#include <constant_strings.h>

#include "camera.h"
#include "config.h"
#include "nodes/nodes.h"
#include "utils.h"
#include "rendersettings_utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (color)
    (depth)
    ((aovSetting, "arnold:"))
    ((aovSettingFilter, "arnold:filter"))
    ((arnoldFormat, "arnold:format"))
    ((aovDriverFormat, "driver:parameters:aov:format"))
    ((aovDriverName, "driver:parameters:aov:name"))
    ((tolerance, "arnold:layer_tolerance"))
    ((enableFiltering, "arnold:layer_enable_filtering"))
    ((halfPrecision, "arnold:layer_half_precision"))
    (request_imager_update)
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

#ifdef HYDRA_NORMALIZE_DEPTH
    static const char* _depthOutputValue = "P VECTOR";
#else
    static const char* _depthOutputValue = "Z FLOAT";
#endif

namespace {

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

const TfToken _GetTokenFromHdFormat(HdFormat format)
{
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

const TfToken _GetTokenFromRenderBufferType(const HdRenderBuffer* buffer)
{
    // Use a wide type to make sure all components are set.
    if (Ai_unlikely(buffer == nullptr)) {
        return _tokens->color4f;
    }
    return _GetTokenFromHdFormat(buffer->GetFormat());
}

CameraUtilFraming _GetFraming(const HdRenderPassStateSharedPtr& renderPassState)
{
    const auto& framing = renderPassState->GetFraming();
    if (framing.IsValid()) {
        return framing;
    } else {
        // For applications that use the old viewport API instead of
        // the new camera framing API.
        const auto& viewport = renderPassState->GetViewport();
        const auto viewportRect = GfRect2i(
            GfVec2i(int(viewport[0]), int(viewport[1])), 
            int(viewport[2]), int(viewport[3])
        );
        return CameraUtilFraming(viewportRect);
    }
}

void _ReadNodeParameters(AtNode* node, const TfToken& prefix, const HdAovSettingsMap& settings,
    HdArnoldRenderDelegate *renderDelegate)
{
    const AtNodeEntry* nodeEntry = AiNodeGetNodeEntry(node);
    for (const auto& setting : settings) {
        if (TfStringStartsWith(setting.first, prefix)) {
            const AtString parameterName(setting.first.GetText() + prefix.size());
            // name is special in arnold
            if (parameterName == str::name) {
                continue;
            }
            const auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, parameterName);
            if (paramEntry != nullptr) {
                HdArnoldSetParameter(node, paramEntry, setting.second, renderDelegate);
            }
        }
    }
};

AtNode* _CreateFilter(HdArnoldRenderDelegate* renderDelegate, const HdAovSettingsMap& aovSettings, int filterIndex)
{
    // We need to make sure that it's holding a string, then try to create it to make sure
    // it's a node type supported by Arnold.
    const auto filterType = _GetOptionalSetting(aovSettings, _tokens->aovSettingFilter, std::string{});
    if (filterType.empty()) {
        return nullptr;
    }
    const auto filterNameStr =
        renderDelegate->GetLocalNodeName(AtString{TfStringPrintf("HdArnoldRenderPass_filter_%d", filterIndex).c_str()});

    AtNode* filter = renderDelegate->FindOrCreateArnoldNode(AtString(filterType.c_str()), filterNameStr);
    if (filter == nullptr) {
        return nullptr;
    }
    
    // We are first checking for the filter parameters prefixed with "arnold:", then doing a second
    // loop to check for "arnold:filter_type:" prefixed parameters. The reason for two loops is
    // we want the second version to overwrite the first one, and with unordered_map, we are not
    // getting any sort of ordering.
    _ReadNodeParameters(filter, _tokens->aovSetting, aovSettings, renderDelegate);
    _ReadNodeParameters(
        filter, TfToken{TfStringPrintf("%s%s:", _tokens->aovSetting.GetText(), filterType.c_str())}, aovSettings, 
        renderDelegate);
    return filter;
}

void _DisableBlendOpacity(AtNode* node)
{
    if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), str::blend_opacity) != nullptr) {
        AiNodeSetBool(node, str::blend_opacity, false);
    }
}

const std::string _CreateAOV(
    HdArnoldRenderDelegate* renderDelegate, const ArnoldAOVTypes& arnoldTypes, const std::string& name,
    const TfToken& sourceType, const std::string& sourceName, AtNode*& writer, AtNode*& reader,
    std::vector<AtString>& lightPathExpressions, std::vector<AtNode*>& aovShaders)
{
    if (sourceType == _tokens->lpe) {
        // We have to add the light path expression to the outputs node in the format of:
        // "aov_name lpe" like "beauty C.*"
        lightPathExpressions.emplace_back(TfStringPrintf("%s %s", name.c_str(), sourceName.c_str()).c_str());
        return name;
    } else if (sourceType == _tokens->primvar) {
        const AtString writerName = renderDelegate->GetLocalNodeName(
            AtString{TfStringPrintf("HdArnoldRenderPass_aov_writer_%s", name.c_str()).c_str()});
        const AtString readerName = renderDelegate->GetLocalNodeName(
            AtString{TfStringPrintf("HdArnoldRenderPass_aov_reader_%s", name.c_str()).c_str()});

        // We need to add a aov write shader to the list of aov_shaders on the options node. Each
        // of this shader will be executed on every surface.
        writer = renderDelegate->FindOrCreateArnoldNode(arnoldTypes.aovWrite, writerName);
        if (sourceName == "st" || sourceName == "uv") { // st and uv are written to the built-in UV
            reader = renderDelegate->FindOrCreateArnoldNode(str::utility, readerName);
            AiNodeSetStr(reader, str::color_mode, str::uv);
            AiNodeSetStr(reader, str::shade_mode, str::flat);
        } else {
            reader = renderDelegate->FindOrCreateArnoldNode(arnoldTypes.userData, readerName);
            AiNodeSetStr(reader, str::attribute, AtString(sourceName.c_str()));
        }
        
        AiNodeSetStr(writer, str::aov_name, AtString(name.c_str()));
        _DisableBlendOpacity(writer);
        AiNodeLink(reader, str::aov_input, writer);
        aovShaders.push_back(writer);
        return name;
    } else {
        return sourceName;
    }
}

} // namespace

HdArnoldRenderPass::HdArnoldRenderPass(
    HdArnoldRenderDelegate* renderDelegate, HdRenderIndex* index, const HdRprimCollection& collection)
    : HdRenderPass(index, collection),
      _fallbackColor(SdfPath::EmptyPath()),
      _fallbackDepth(SdfPath::EmptyPath()),
      _fallbackPrimId(SdfPath::EmptyPath()),
      _renderDelegate(renderDelegate)
{
    auto* universe = _renderDelegate->GetUniverse();
    _camera = _renderDelegate->CreateArnoldNode(str::persp_camera, 
        _renderDelegate->GetLocalNodeName(str::renderPassCamera));
    AiNodeSetPtr(AiUniverseGetOptions(universe), str::camera, _camera);
    const auto defaultFilter = TfGetEnvSetting(HDARNOLD_default_filter);
    AtString filterStr(defaultFilter.c_str());
    
    // In case the defaultFilter string is an invalid filter type.
    const AtNodeEntry *filterEntry = AiNodeEntryLookUp(filterStr);
    if (filterEntry == nullptr || AiNodeEntryGetType(filterEntry) != AI_NODE_FILTER)
        filterStr = str::box_filter;

    const auto defaultFilterAttributes = TfGetEnvSetting(HDARNOLD_default_filter_attributes);
    _defaultFilter = _renderDelegate->CreateArnoldNode(filterStr,
        _renderDelegate->GetLocalNodeName(str::renderPassFilter));
    
    if (!defaultFilterAttributes.empty()) {
        AiNodeSetAttributes(_defaultFilter, defaultFilterAttributes.c_str());
    }
    _closestFilter = _renderDelegate->CreateArnoldNode(str::closest_filter,
        _renderDelegate->GetLocalNodeName(str::renderPassClosestFilter));
    
    _mainDriver = _renderDelegate->CreateArnoldNode(str::HdArnoldDriverMain,
        _renderDelegate->GetLocalNodeName(str::renderPassMainDriver));
    _primIdWriter = _renderDelegate->CreateArnoldNode(str::aov_write_int,
        _renderDelegate->GetLocalNodeName(str::renderPassPrimIdWriter));
    
    AiNodeSetStr(_primIdWriter, str::aov_name, str::hydraPrimId);
    _primIdReader = _renderDelegate->CreateArnoldNode(str::user_data_int,
        _renderDelegate->GetLocalNodeName(str::renderPassPrimIdReader));
    
    AiNodeSetStr(_primIdReader, str::attribute, str::hydraPrimId);
    AiNodeLink(_primIdReader, str::aov_input, _primIdWriter);

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
        TfStringPrintf("%s %s %s", _depthOutputValue, AiNodeGetName(_closestFilter), AiNodeGetName(_mainDriver));
    const auto idString = TfStringPrintf(
        "%s INT %s %s", str::hydraPrimId.c_str(), AiNodeGetName(_closestFilter), AiNodeGetName(_mainDriver));
    AiArraySetStr(_fallbackOutputs, 0, beautyString.c_str());
    AiArraySetStr(_fallbackOutputs, 1, positionString.c_str());
    AiArraySetStr(_fallbackOutputs, 2, idString.c_str());
    _fallbackAovShaders = AiArrayAllocate(1, 1, AI_TYPE_POINTER);
    AiArraySetPtr(_fallbackAovShaders, 0, _primIdWriter);

    const auto& config = HdArnoldConfig::GetInstance();
    AiNodeSetFlt(_camera, str::shutter_start, config.shutter_start);
    AiNodeSetFlt(_camera, str::shutter_end, config.shutter_end);
}

HdArnoldRenderPass::~HdArnoldRenderPass()
{
    reinterpret_cast<HdArnoldRenderParam*>(_renderDelegate->GetRenderParam())->Interrupt();
    _renderDelegate->DestroyArnoldNode(_camera);
    _renderDelegate->DestroyArnoldNode(_defaultFilter);
    _renderDelegate->DestroyArnoldNode(_closestFilter);
    _renderDelegate->DestroyArnoldNode(_mainDriver);
    _renderDelegate->DestroyArnoldNode(_primIdWriter);
    _renderDelegate->DestroyArnoldNode(_primIdReader);
    // We are not assigning this array to anything, so needs to be manually destroyed.
    AiArrayDestroy(_fallbackOutputs);
    AiArrayDestroy(_fallbackAovShaders);
    
    for (auto& customProduct : _customProducts) {
        if (customProduct.driver != nullptr) {
            _renderDelegate->DestroyArnoldNode(customProduct.driver);
        }
        if (customProduct.filter != nullptr) {
            _renderDelegate->DestroyArnoldNode(customProduct.filter);
        }
        for (auto& renderVar : customProduct.renderVars) {
            if (renderVar.writer != nullptr) {
                _renderDelegate->DestroyArnoldNode(renderVar.writer);
            }
            if (renderVar.reader != nullptr) {
                _renderDelegate->DestroyArnoldNode(renderVar.reader);
            }
        }
    }
    _ClearRenderBuffers();
}

void HdArnoldRenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags)
{
    _renderDelegate->SetRenderTags(renderTags);
    auto* renderParam = reinterpret_cast<HdArnoldRenderParam*>(_renderDelegate->GetRenderParam());

    AtNode *options = AiUniverseGetOptions(_renderDelegate->GetUniverse());
    bool isOrtho = false;
    const auto* currentUniverseCamera =
        static_cast<const AtNode*>(AiNodeGetPtr(options, str::camera));
    const auto* camera = reinterpret_cast<const HdArnoldCamera*>(renderPassState->GetCamera());
    const SdfPath cameraId = camera ? camera->GetId() : SdfPath();
    const auto useOwnedCamera = camera == nullptr;
    AtNode* currentCamera = nullptr;
    // If camera is nullptr from the render pass state, we are using a camera created by the renderpass.
    if (useOwnedCamera) {
        currentCamera = _camera;
        if (currentUniverseCamera != _camera) {
            renderParam->Interrupt();
            AiNodeSetPtr(options, str::camera, _camera);
        }
    } else {
        currentCamera = camera->GetCamera();
        if (currentUniverseCamera != currentCamera) {
            renderParam->Interrupt();
            AiNodeSetPtr(options, str::camera, currentCamera);
        }
        // TODO: We should test the type of the arnold camera instead ?
        isOrtho =  camera->GetProjection() == HdCamera::Projection::Orthographic;
    }

    const auto projMtx = renderPassState->GetProjectionMatrix();
    const auto viewMtx = renderPassState->GetWorldToViewMatrix();
    if (projMtx != _projMtx || viewMtx != _viewMtx) {
        _projMtx = projMtx;
        _viewMtx = viewMtx;
        renderParam->Interrupt(true, false);
        auto* mainDriverData = static_cast<DriverMainData*>(AiNodeGetLocalData(_mainDriver));
        if (mainDriverData != nullptr) {
            mainDriverData->projMtx = GfMatrix4f{_projMtx};
            mainDriverData->viewMtx = GfMatrix4f{_viewMtx};
        } else {
            AtMatrix projMtx; 
            ConvertValue(projMtx, _projMtx);
            AiNodeSetMatrix(_mainDriver, str::projMtx, projMtx);
            AtMatrix viewMtx;
            ConvertValue(viewMtx, _viewMtx);
            AiNodeSetMatrix(_mainDriver, str::viewMtx, viewMtx);
        }

        if (currentCamera && isOrtho) {  // TODO do it once, if proj or size has changed
            GfVec4f screen = HdArnoldCamera::GetScreenWindowFromOrthoProjection(projMtx);
            AiNodeSetVec2(_camera, str::screen_window_min, screen[0], screen[1]);
            AiNodeSetVec2(_camera, str::screen_window_max, screen[2], screen[3]);
        }

        if (useOwnedCamera) {
            const auto fov = static_cast<float>(GfRadiansToDegrees(atan(1.0 / _projMtx[0][0]) * 2.0));
            AiNodeSetFlt(_camera, str::fov, fov);
            AtMatrix invViewMtx;
            ConvertValue(invViewMtx, _viewMtx.GetInverse());
            AiNodeSetMatrix(_camera, str::matrix, invViewMtx);
        }
    }

    CameraUtilFraming newFraming = _GetFraming(renderPassState);
    GfVec2i delegateResolution = _renderDelegate->GetResolution();
    int width = static_cast<int>(newFraming.displayWindow.GetSize()[0]);
    int height = static_cast<int>(newFraming.displayWindow.GetSize()[1]);
    
    if (delegateResolution[0] > 0 && delegateResolution[1] > 0 && 
        delegateResolution[0] != width && delegateResolution[1] != height) {

        // If a resolution is provided through the render settings, we use
        // that instead of the viewport.
        width = delegateResolution[0];
        height = delegateResolution[1];
        newFraming = CameraUtilFraming(GfRect2i(
            GfVec2i(0, 0), width, height));
    }

    const bool framingChanged = newFraming != _framing;
    GfVec4f windowNDC = _renderDelegate->GetWindowNDC();
    float pixelAspectRatio = _renderDelegate->GetPixelAspectRatio();
    // check if we have a non-default window
    bool hasWindowNDC = (!GfIsClose(windowNDC[0], 0.0f, AI_EPSILON)) || 
                        (!GfIsClose(windowNDC[1], 0.0f, AI_EPSILON)) || 
                        (!GfIsClose(windowNDC[2], 1.0f, AI_EPSILON)) || 
                        (!GfIsClose(windowNDC[3], 1.0f, AI_EPSILON));
    // check if the window has changed since the last _Execute
    bool windowChanged = (!GfIsClose(windowNDC[0], _windowNDC[0], AI_EPSILON)) || 
                        (!GfIsClose(windowNDC[1], _windowNDC[1], AI_EPSILON)) || 
                        (!GfIsClose(windowNDC[2], _windowNDC[2], AI_EPSILON)) || 
                        (!GfIsClose(windowNDC[3], _windowNDC[3], AI_EPSILON));

    auto clearBuffers = [&](HdArnoldRenderBufferStorage& storage, bool allocate, int w, int h) {

        static std::vector<uint8_t> zeroData;
        zeroData.resize(w * h * 4);

        for (auto& buffer : storage) {
            HdArnoldRenderBuffer *renderBuffer = buffer.second.buffer;
            if (renderBuffer != nullptr && !renderBuffer->IsEmpty()) {
                
                if (allocate && (renderBuffer->GetWidth() != w || renderBuffer->GetHeight() != h))
                    renderBuffer->Allocate(GfVec3i(w, h, 0), renderBuffer->GetFormat(), renderBuffer->IsMultiSampled());

                renderBuffer->WriteBucket(0, 0, w, h, HdFormatUNorm8Vec4, zeroData.data());
            }
        }
    };

    if (framingChanged) {
        // The render resolution has changed, we need to update the arnold options
        renderParam->Interrupt(true, false);
        _framing = newFraming;
        auto* options = _renderDelegate->GetOptions();
        AiNodeSetInt(options, str::xres, width);
        AiNodeSetInt(options, str::yres, height);

        clearBuffers(_renderBuffers, true, width, height);
        AiNodeSetInt(options, str::region_min_x, _framing.dataWindow.GetMinX());
        AiNodeSetInt(options, str::region_max_x, _framing.dataWindow.GetMaxX());
        AiNodeSetInt(options, str::region_min_y, _framing.dataWindow.GetMinY());
        AiNodeSetInt(options, str::region_max_y, _framing.dataWindow.GetMaxY());
        // With the ortho camera we need to update the screen_window_min/max when the window changes
        // This is unfortunate as we won't be able to have multiple viewport with the same ortho camera
        // Another option would be to keep an ortho camera on this class and update it ?
        if (currentCamera && isOrtho) {
            GfVec4f screen = HdArnoldCamera::GetScreenWindowFromOrthoProjection(projMtx);
            AiNodeSetVec2(_camera, str::screen_window_min, screen[0], screen[1]);
            AiNodeSetVec2(_camera, str::screen_window_max, screen[2], screen[3]);
        }

        // if we have a window, then we need to recompute it anyway
        if (hasWindowNDC)
            windowChanged = true;
    }

    if (windowChanged) {
        renderParam->Interrupt(true, false);
        if (hasWindowNDC) {
            _windowNDC = windowNDC;
            
            // Need to invert the window range in the Y axis
            float minY = 1. - windowNDC[3];
            float maxY = 1. - windowNDC[1];
            windowNDC[1] = minY;
            windowNDC[3] = maxY;

            // Ensure the user isn't setting invalid ranges
            if (windowNDC[0] > windowNDC[2])
                std::swap(windowNDC[0], windowNDC[2]);
            if (windowNDC[1] > windowNDC[3])
                std::swap(windowNDC[1], windowNDC[3]);


            // return the min region in a given axis X or Y, provided the input data that we receive from hydra
            const auto getAxisRegion = [&](float windowMin, float windowMax, int settingsRes, int bufferRes) -> GfVec2i {
                // if an explicit render settings resolution was provided, we want to use it, otherwise we use the 
                // render buffer resolution
                float regionMinFlt = windowMin * (settingsRes > 0 ? settingsRes : bufferRes);
                float regionMaxFlt = windowMax * (settingsRes > 0 ? settingsRes : bufferRes) - 1;
                GfVec2i region(std::round(regionMinFlt), std::round(regionMaxFlt));

                if (settingsRes <= 0) {
                    // In the arnold options attributes, we need 
                    // region_max_x - region_min_x = width - 1
                    // region_max_y - region_min_y = height - 1
                    // so that the render buffer matches the expected output. 
                    int mismatchDelta = region[1] - region[0] - bufferRes + 1;
                    if (mismatchDelta != 0) {
                        // There could have been a precision issue, in that case we want to adjust either the region min or the max
                        float deltaMin = std::abs(regionMinFlt - region[0]);
                        float deltaMax = std::abs(regionMaxFlt - region[1]);
                        // We want to tweak whichever between min & max float value is the most distant from the 
                        // rounded integer we used
                        if (deltaMin > deltaMax)
                            region[0] += mismatchDelta > 0 ? 1 : -1;
                        // if deltaMax is higher, then it's the regionMax that will automatically be tweaked,
                        // here we are just returning the region min
                    }
                    region[1] = region[0] + bufferRes - 1;
                }
                return region;
            };

            // we want the output render buffer to have a resolution equal to 
            // width/height. This means we need to adjust xres/yres, so that
            // region min/max corresponds to the render resolution
            float xDelta = windowNDC[2] - windowNDC[0]; // maxX - minX
            float yDelta = windowNDC[3] - windowNDC[1]; // maxY - minY

            if (xDelta > AI_EPSILON) {
                float xInvDelta = 1.f / xDelta;
                // If no resolution was explicitely set in the render settings, 
                // we use the framing window which has possibly been affected by 
                // the dataWindowNDC, providing only the renderable buffer size.
                // In this case, we need to extrapolate and find what is the 
                // "full" resolution that would provide the expected buffer size for 
                // this windowNDC
                if (delegateResolution[0] <= 0) {
                    AiNodeSetInt(options, str::xres, std::round(width * (xInvDelta)));
                    // Normalize windowNDC so that its delta is 1
                    windowNDC[0] *= xInvDelta;
                    windowNDC[2] *= xInvDelta;
                }                
            }
            
            GfVec2i regionX = getAxisRegion(windowNDC[0], windowNDC[2], delegateResolution[0], width);

            AiNodeSetInt(options, str::region_min_x, regionX[0]);
            AiNodeSetInt(options, str::region_max_x, regionX[1]);
            
            if (yDelta > AI_EPSILON) {
                float yInvDelta = 1.f / yDelta;
                if (delegateResolution[1] <= 0) {
                    AiNodeSetInt(options, str::yres, std::round(height * (yInvDelta)));
                    windowNDC[1] *= yInvDelta;    
                    windowNDC[3] *= yInvDelta;
                }
               
                // For interactive renders, need to adjust the pixel aspect ratio to match the window NDC
                if (!_renderDelegate->IsBatchContext()) {
                    pixelAspectRatio *= xDelta / yDelta;
                }
            
            } 
            GfVec2i regionY = getAxisRegion(windowNDC[1], windowNDC[3], delegateResolution[1], height);
            AiNodeSetInt(options, str::region_min_y, regionY[0]);
            AiNodeSetInt(options, str::region_max_y, regionY[1]);

            clearBuffers(_renderBuffers, true, regionX[1] - regionX[0] + 1, regionY[1] - regionY[0] + 1);;
            
        } else {
            // the window was restored to defaults, we need to reset the region
            // attributes, as well as xres,yres, that could have been adjusted
            // in previous iterations
            AiNodeResetParameter(options, str::region_min_x);
            AiNodeResetParameter(options, str::region_min_y);
            AiNodeResetParameter(options, str::region_max_x);
            AiNodeResetParameter(options, str::region_max_y);
            AiNodeSetInt(options, str::xres, width);
            AiNodeSetInt(options, str::yres, height);
            _windowNDC = GfVec4f(0.f, 0.f, 1.f, 1.f);
        }
    }
    float currentPixelAspectRatio = AiNodeGetFlt(options, str::pixel_aspect_ratio);
    if (!GfIsClose(currentPixelAspectRatio, pixelAspectRatio, AI_EPSILON)) {
        renderParam->Interrupt(true, false);
        AiNodeSetFlt(options, str::pixel_aspect_ratio, pixelAspectRatio);
    }    

    auto checkShader = [&] (AtNode* shader, const AtString& paramName) {
        auto* options = _renderDelegate->GetOptions();
        if (shader != static_cast<AtNode*>(AiNodeGetPtr(options, paramName))) {
            renderParam->Interrupt(true, false);
            AiNodeSetPtr(options, paramName, shader);
        }
    };

    checkShader(_renderDelegate->GetBackground(GetRenderIndex()), str::background);
    checkShader(_renderDelegate->GetAtmosphere(GetRenderIndex()), str::atmosphere);
    checkShader(_renderDelegate->GetShaderOverride(GetRenderIndex()), str::shader_override);

    // check if the user aov shaders have changed
    auto aovShaders = _renderDelegate->GetAovShaders(GetRenderIndex());
    bool updateAovs = false;
    if (_aovShaders != aovShaders) {
        _aovShaders = aovShaders;
        updateAovs = true;
    }

    bool updateImagers = false;
    AtNode* imager = _renderDelegate->GetImager(GetRenderIndex());
    if (imager != static_cast<AtNode*>(AiNodeGetPtr(_mainDriver, str::input)))
        updateImagers = true;

    // Eventually set the subdiv dicing camera in the options
    const AtNode *subdivDicingCamera = _renderDelegate->GetSubdivDicingCamera(GetRenderIndex());
    const AtNode *currentSubdivDicingCamera = (const AtNode*)AiNodeGetPtr(options, str::subdiv_dicing_camera);
    if (currentSubdivDicingCamera != subdivDicingCamera) {
        renderParam->Interrupt(true, false);
        AiNodeSetPtr(options, str::subdiv_dicing_camera, (void*)subdivDicingCamera);
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

    TF_VERIFY(!aovBindings.empty(), "No AOV bindings to render into!");

    // AOV bindings exists, so first we are checking if anything has changed.
    // If something has changed, then we rebuild the local storage class, and the outputs definition.
    // We expect Hydra to resize the render buffers.
    const bool needsDelegateProductsUpdate = _renderDelegate->NeedsDelegateProductsUpdate();
    
    if (_RenderBuffersChanged(aovBindings) || needsDelegateProductsUpdate ||
        _usingFallbackBuffers || updateAovs || updateImagers) {
        _usingFallbackBuffers = false;
        renderParam->Interrupt();
        if (_mainDriver)
            AiNodeResetParameter(_mainDriver, str::render_outputs);

        _ClearRenderBuffers();
        _renderDelegate->ClearCryptomatteDrivers();
        AiNodeSetPtr(_mainDriver, str::color_pointer, nullptr);
        AiNodeSetPtr(_mainDriver, str::depth_pointer, nullptr);
        AiNodeSetPtr(_mainDriver, str::id_pointer, nullptr);
        // Rebuilding render buffers
        const auto numBindings = static_cast<unsigned int>(aovBindings.size());
        std::vector<AtString> outputs;
        outputs.reserve(numBindings);
        std::vector<AtString> lightPathExpressions;
        std::vector<AtNode*> aovShaders;
        // When creating the outputs array we follow this logic:
        // - color -> RGBA RGBA for the beauty box filter by default
        // - depth -> Z FLOAT closest filter by default
        //     (if HYDRA_NORMALIZE_DEPTH is defined, use P VECTOR instead)
        // - primId -> ID UINT closest filter by default
        // - everything else -> aovName RGB closest filter by default
        // We are using box filter for the color and closest for everything else.
        const auto* boxName = AiNodeGetName(_defaultFilter);
        const auto* closestName = AiNodeGetName(_closestFilter);
        const auto* mainDriverName = AiNodeGetName(_mainDriver);
        int bufferIndex = 0;
        int filterIndex = 0;
        std::vector<AtString> buffer_names;
        std::vector<void*> buffer_pointers;

        for (const auto& binding : aovBindings) {
            auto& buffer = _renderBuffers[binding.aovName];
            // Sadly we only get a raw pointer here, so we have to expect hydra not clearing up render buffers
            // while they are being used.
            buffer.buffer = dynamic_cast<HdArnoldRenderBuffer*>(binding.renderBuffer);
            buffer.settings = binding.aovSettings;
            buffer.filter = _CreateFilter(_renderDelegate, binding.aovSettings, ++filterIndex);
            const auto* filterName = buffer.filter != nullptr ? AiNodeGetName(buffer.filter) : boxName;
            // Different possible filter for P and ID AOVs.
            const auto* filterGeoName = buffer.filter != nullptr ? AiNodeGetName(buffer.filter) : closestName;
            const auto sourceType =
                _GetOptionalSetting<TfToken>(binding.aovSettings, _tokens->sourceType, _tokens->raw);
            const auto sourceName = _GetOptionalSetting<std::string>(
                binding.aovSettings, _tokens->sourceName, binding.aovName.GetString());

            // The beauty output will show up as a LPE AOV called "color" with the expression as "C.*"
            // But Arnold won't recognize this as being the actual beauty and adaptive sampling
            // won't apply properly (see #1006). So we want to detect which output is the actual beauty 
            // and treat it as Arnold would expect.
            bool isBeauty = binding.aovName == HdAovTokens->color;
            
            // When using a raw buffer, we have special behavior for color, depth and ID. Otherwise we are creating
            // an aov with the same name. We can't just check for the source name; for example: using a primvar
            // type and displaying a "color" or a "depth" user data is a valid use case.
            const auto isRaw = sourceType == _tokens->raw;
            AtString output;
            if (isRaw && sourceName == HdAovTokens->color) {
                output = AtString{TfStringPrintf("RGBA RGBA %s %s", filterName, mainDriverName).c_str()};
                AiNodeSetPtr(_mainDriver, str::color_pointer, binding.renderBuffer);
            } else if (isRaw && sourceName == HdAovTokens->depth) {
                output = AtString{TfStringPrintf("%s %s %s", _depthOutputValue, filterGeoName, mainDriverName).c_str()};
                AiNodeSetPtr(_mainDriver, str::depth_pointer, binding.renderBuffer);
            } else if (isRaw && sourceName == HdAovTokens->primId) {
                aovShaders.push_back(_primIdWriter);
                output =
                    AtString{TfStringPrintf("%s INT %s %s", str::hydraPrimId.c_str(), filterGeoName, mainDriverName)
                                 .c_str()};
                AiNodeSetPtr(_mainDriver, str::id_pointer, binding.renderBuffer);
            } else {
                // Querying the data format from USD, with a default value of color3f.
                TfToken format = _GetOptionalSetting<TfToken>(
                    binding.aovSettings, _tokens->dataType, _GetTokenFromRenderBufferType(buffer.buffer));

                const auto driverFormatIt = binding.aovSettings.find(_tokens->aovDriverFormat);
                if (driverFormatIt != binding.aovSettings.end()) {
                    if (driverFormatIt->second.IsHolding<TfToken>())
                        format = driverFormatIt->second.UncheckedGet<TfToken>();
                    else if (driverFormatIt->second.IsHolding<std::string>())
                        format = TfToken(driverFormatIt->second.UncheckedGet<std::string>());
                }

                const auto it = binding.aovSettings.find(_tokens->arnoldFormat);
                if (it != binding.aovSettings.end()) {
                    if (it->second.IsHolding<TfToken>())
                        format = it->second.UncheckedGet<TfToken>();
                    else if (it->second.IsHolding<std::string>())
                        format = TfToken(it->second.UncheckedGet<std::string>());
                }

                // const auto arnoldTypes = _GetArnoldAOVTypeFromTokenType(format);
                const ArnoldAOVTypes arnoldTypes = GetArnoldTypesFromFormatToken(format);

                const char* aovName = nullptr;
                // The beauty output will show up as a lpe but we want to treat it differently
                if (sourceType == _tokens->lpe && !isBeauty) {
                    aovName = binding.aovName.GetText();
                    // We have to add the light path expression to the outputs node in the format of:
                    // "aov_name lpe" like "beauty C.*"
                    lightPathExpressions.emplace_back(
                        TfStringPrintf("%s %s", binding.aovName.GetText(), sourceName.c_str()).c_str());
                } else if (sourceType == _tokens->primvar) {
                    aovName = binding.aovName.GetText();
                    const auto writerName = _renderDelegate->GetLocalNodeName(
                        AtString{TfStringPrintf("HdArnoldRenderPass_aov_writer_%s", aovName).c_str()});
                    const auto readerName = _renderDelegate->GetLocalNodeName(
                        AtString{TfStringPrintf("HdArnoldRenderPass_aov_reader_%s", aovName).c_str()});

                    // We need to add a aov write shader to the list of aov_shaders on the options node. Each
                    // of this shader will be executed on every surface.
                    buffer.writer = _renderDelegate->FindOrCreateArnoldNode(arnoldTypes.aovWrite,
                        writerName);
                    if (sourceName == "st" || sourceName == "uv") { // st and uv are written to the built-in UV
                        buffer.reader = _renderDelegate->FindOrCreateArnoldNode(str::utility,
                            readerName);
                        AiNodeSetStr(buffer.reader, str::color_mode, str::uv);
                        AiNodeSetStr(buffer.reader, str::shade_mode, str::flat);
                    } else {
                        buffer.reader = _renderDelegate->FindOrCreateArnoldNode(AtString(arnoldTypes.userData.c_str()),
                            AtString(sourceName.c_str()));
                    }
                    
                    AiNodeSetStr(buffer.writer, str::aov_name, AtString(aovName));
                    _DisableBlendOpacity(buffer.writer);
                    AiNodeLink(buffer.reader, str::aov_input, buffer.writer);
                    aovShaders.push_back(buffer.writer);
                } else {
                    // the beauty output should be called "RGBA" for arnold
                    aovName = isBeauty ? "RGBA" : sourceName.c_str();
                }
                std::string layerName(aovName);
                layerName = _GetOptionalSetting<std::string>(
                    binding.aovSettings, _tokens->aovDriverName, layerName);

                // If this driver is meant for one of the cryptomatte AOVs, it will be filled with the 
                // cryptomatte metadatas through the user data "custom_attributes". We want to store 
                // the driver node names in the render delegate, so that we can lookup this user data
                // during GetRenderStats
                if (binding.aovName == str::t_crypto_asset || 
                    binding.aovName == str::t_crypto_material ||
                    binding.aovName == str::t_crypto_object)
                    _renderDelegate->RegisterCryptomatteDriver(AtString(mainDriverName));
                
                buffer_pointers.push_back((void*)buffer.buffer);
                buffer_names.push_back(AtString(layerName.c_str()));                

                output = AtString{
                    TfStringPrintf(
                        "%s %s %s %s %s", aovName, arnoldTypes.outputString, filterName, mainDriverName, layerName.c_str())
                        .c_str()};

            }
            outputs.push_back(output);
        }
        if (buffer_names.empty() || buffer_names.size() != buffer_pointers.size()) {
            AiNodeResetParameter(_mainDriver, str::buffer_names);
            AiNodeResetParameter(_mainDriver, str::buffer_pointers);
        } else {
            AiNodeSetArray(_mainDriver, str::buffer_names, AiArrayConvert(buffer_names.size(), 1, AI_TYPE_STRING, &buffer_names[0]));
            AiNodeSetArray(_mainDriver, str::buffer_pointers, AiArrayConvert(buffer_pointers.size(), 1, AI_TYPE_POINTER, &buffer_pointers[0]));
        }
        // We haven't initialized the custom products yet.
        // At the moment this won't work if delegate render products are set interactively, as this is only meant to
        // override the output driver for batch renders. In Solaris, 
        // delegate render products are only set when rendering in husk.
        if (needsDelegateProductsUpdate) {
            const auto& delegateRenderProducts = _renderDelegate->GetDelegateRenderProducts();
            _customProducts.clear();
            _customProducts.reserve(delegateRenderProducts.size());
            // Get an eventual output override string. We only want to use it if no outputs 
            // were added above with hydra drives, since they will render to the same filename
            // and we don't want several drivers writing to the same image
            const std::string &outputOverride = _renderDelegate->GetOutputOverride();
            for (const auto& product : delegateRenderProducts) {
                CustomProduct customProduct;
                if (product.renderVars.empty()) {
                    continue;
                }

                // Output overrides can be set to force an output filename.
                // However we don't always want to do this for arnold product types
                // to avoid having multiple drivers writing to the same filename #2187
                bool hasOutputOverride = !outputOverride.empty();
                if (hasOutputOverride) {
                    // Check if one of this render product's AOVs is the beauty.
                    // If not, we'll ignore the output override
                    bool hasBeauty = false;
                    for (const auto& renderVar : product.renderVars) {
                        if (renderVar.sourceName == HdAovTokens->color || renderVar.sourceName == "RGBA") {
                            hasBeauty = true;
                            break;
                        }
                    }
                    if (!outputs.empty() && !hasBeauty)
                        hasOutputOverride = false;
                }
                const AtString customDriverName =
                    AtString{TfStringPrintf("HdArnoldRenderPass_driver_%s_%d", product.productType.GetText(), ++bufferIndex).c_str()};
                customProduct.driver = _renderDelegate->FindOrCreateArnoldNode(AtString(product.productType.GetText()),
                    customDriverName);
                if (Ai_unlikely(customProduct.driver == nullptr)) {
                    continue;
                }

                if (!hasOutputOverride) {
                    // default use case : set the product name as the output image filename
                    AiNodeSetStr(customProduct.driver, str::filename, AtString(product.productName.GetText()));
                }
                else {
                    // If the delegate has an output image override, we want to use this for this driver.
                    // Note that we can only use it once as multiple drivers pointing to the same filename
                    // will cause errors
                    AiNodeSetStr(customProduct.driver, str::filename, AtString(outputOverride.c_str()));
                    hasOutputOverride = false;
                }
                // One filter per custom driver.
                customProduct.filter = _CreateFilter(_renderDelegate, product.settings, ++filterIndex);
                const auto* filterName =
                    customProduct.filter != nullptr ? AiNodeGetName(customProduct.filter) : boxName;
                // Applying custom parameters to the driver.
                // First we read parameters simply prefixed with arnold: (do we still need this ?)
                _ReadNodeParameters(customProduct.driver, _tokens->aovSetting, product.settings, _renderDelegate);

                // Then we read parameters prefixed with arnold:{driverType}: (e.g. arnold:driver_exr:)
                std::string driverPrefix = std::string("arnold:") + product.productType.GetString() + std::string(":");
                _ReadNodeParameters(customProduct.driver, TfToken(driverPrefix.c_str()), product.settings, _renderDelegate);

                // Arnold supports multiple deepexr settings per AOV, by setting the parameters
                // layer_tolerance, layer_half_precision, layer_enable_filtering.
                // If we see those parameters set on RenderVars, we want to set those array 
                // attributes accordingly (#2260)
                const bool isDeepExrDriver = AiNodeIs(customProduct.driver, str::driver_deepexr);
                const auto numRenderVars = static_cast<uint32_t>(product.renderVars.size());
                std::vector<float> tolerances;
                std::vector<bool> enableFiltering;
                std::vector<bool> halfPrecision;               

                // Loop through render vars in case we have AOV-specific parameters
                for (const auto& renderVar : product.renderVars) {
                    CustomRenderVar customRenderVar;
                    int renderVarIndex = customProduct.renderVars.size();

                    const auto toleranceIt = renderVar.settings.find(_tokens->tolerance);
                    if (toleranceIt != renderVar.settings.end() && toleranceIt->second.IsHolding<float>()) {
                        // The array attribute layer_tolerance should default to the value set in the driver                    
                        if (tolerances.empty())
                            tolerances.assign(numRenderVars, AiNodeGetFlt(customProduct.driver, str::depth_tolerance));
                        tolerances[renderVarIndex] = toleranceIt->second.UncheckedGet<float>();
                    }
                    const auto enableFilteringIt = renderVar.settings.find(_tokens->enableFiltering);
                    if (enableFilteringIt != renderVar.settings.end() && enableFilteringIt->second.IsHolding<bool>()) {
                        if (enableFiltering.empty())
                            enableFiltering.assign(numRenderVars, true);
                        enableFiltering[renderVarIndex] = enableFilteringIt->second.UncheckedGet<bool>();
                    }
                    const auto halfPrecisionIt = renderVar.settings.find(_tokens->halfPrecision);
                    if (halfPrecisionIt != renderVar.settings.end() && halfPrecisionIt->second.IsHolding<bool>()) {
                        if (halfPrecision.empty())
                            halfPrecision.assign(numRenderVars, AiNodeGetFlt(customProduct.driver, str::depth_half_precision));
                        halfPrecision[renderVarIndex] = halfPrecisionIt->second.UncheckedGet<bool>();
                    }

                    const auto isRaw = renderVar.sourceType == _tokens->raw;
                    if (isRaw && renderVar.sourceName == HdAovTokens->color) {
                        customRenderVar.output =
                            AtString{TfStringPrintf("RGBA RGBA %s %s", filterName, customDriverName.c_str()).c_str()};
                    } else if (isRaw && renderVar.sourceName == HdAovTokens->depth) {
                        customRenderVar.output =
                            AtString{TfStringPrintf("Z FLOAT %s %s", filterName, customDriverName.c_str()).c_str()};
                    } else if (isRaw && renderVar.sourceName == HdAovTokens->primId) {
                        aovShaders.push_back(_primIdWriter);
                        customRenderVar.output = AtString{
                            TfStringPrintf(
                                "%s INT %s %s", str::hydraPrimId.c_str(), filterName, customDriverName.c_str())
                                .c_str()};
                    } else {
                        // Querying the data format from USD, with a default value of color3f.
                        // If we have arnold:format defined, we use its value for the format
                        const TfToken hydraFormat = _GetOptionalSetting<TfToken>(renderVar.settings, _tokens->dataType, _GetTokenFromHdFormat(renderVar.format));
                        const TfToken arnoldFormat = _GetOptionalSetting<TfToken>(renderVar.settings, _tokens->arnoldFormat, TfToken(""));
                        const TfToken driverAovFormat = _GetOptionalSetting<TfToken>(renderVar.settings, _tokens->aovDriverFormat, TfToken(""));
                        const TfToken format = arnoldFormat != TfToken("") ? arnoldFormat : (driverAovFormat != TfToken("") ? driverAovFormat : hydraFormat);
                        const ArnoldAOVTypes arnoldTypes = GetArnoldTypesFromFormatToken(format);

                        const auto aovName = _CreateAOV(
                            _renderDelegate, arnoldTypes, renderVar.name, renderVar.sourceType,
                            renderVar.sourceName, customRenderVar.writer, customRenderVar.reader, lightPathExpressions,
                            aovShaders);
                        
                        if (aovName == "crypto_object" || aovName == "crypto_asset"
                            || aovName == "crypto_material") {
                            _renderDelegate->SetHasCryptomatte(true);
                        }
                        
                        // Check if the AOV has a specific filter
                        const auto arnoldAovFilterName = _GetOptionalSetting<std::string>(renderVar.settings, _tokens->aovSettingFilter, "");
                        AtNode *aovFilterNode = arnoldAovFilterName.empty() ? nullptr : _CreateFilter(_renderDelegate, renderVar.settings, ++filterIndex);
                        std::string output = TfStringPrintf(
                                         "%s %s %s %s", aovName.c_str(), arnoldTypes.outputString, aovFilterNode ? AiNodeGetName(aovFilterNode) : filterName,
                                         customDriverName.c_str());
                        if (!renderVar.name.empty() && renderVar.name != renderVar.sourceName) {
                            output += TfStringPrintf(" %s", renderVar.name.c_str());
                        }
                        if (arnoldTypes.isHalf && !isDeepExrDriver) {
                            output += " HALF";
                        }
                        customRenderVar.output = AtString{output.c_str()};
                    }
                    customProduct.renderVars.push_back(customRenderVar);
                }

                if (isDeepExrDriver) {
                    // For deep exr AOVs, check for AOV-specific values
                    if (!tolerances.empty()) {
                        AiNodeSetArray(customProduct.driver, str::layer_tolerance, 
                            AiArrayConvert(tolerances.size(), 1, AI_TYPE_FLOAT, tolerances.data()));
                    } else {
                        AiNodeResetParameter(customProduct.driver, str::layer_tolerance);
                    }
                    if (!enableFiltering.empty()) {
                        AtArray *filteringArray = AiArrayAllocate(enableFiltering.size(), 1, AI_TYPE_BOOLEAN);
                        bool* filteringValues = static_cast<bool*>(AiArrayMap(filteringArray));
                        for (size_t i = 0; i < enableFiltering.size(); ++i)
                            filteringValues[i] = enableFiltering[i];
                        AiArrayUnmap(filteringArray);
                        AiNodeSetArray(customProduct.driver, str::layer_enable_filtering, filteringArray);
                    } else {
                        AiNodeResetParameter(customProduct.driver, str::layer_enable_filtering);
                    }  
                    if (!halfPrecision.empty()) {
                        AtArray *halfPrecisionArray = AiArrayAllocate(halfPrecision.size(), 1, AI_TYPE_BOOLEAN);
                        bool* halfPrecisionValues = static_cast<bool*>(AiArrayMap(halfPrecisionArray));
                        for (size_t i = 0; i < halfPrecision.size(); ++i)
                            halfPrecisionValues[i] = halfPrecision[i];
                        AiArrayUnmap(halfPrecisionArray);
                        AiNodeSetArray(customProduct.driver, str::layer_half_precision, halfPrecisionArray);

                    } else {
                        AiNodeResetParameter(customProduct.driver, str::layer_half_precision);
                    }
                }
                AiNodeSetPtr(customProduct.driver, str::input, imager);
                _customProducts.push_back(std::move(customProduct));
            }

            if (_customProducts.empty()) {
                // if we didn't manage to create any custom product, we want
                // the render delegate to clear its list. Otherwise the function
                // NeedsDelegateProductsUpdate will keep returning true,
                // triggering changes and the render will start over and over
                _renderDelegate->ClearDelegateRenderProducts();
            }
        }
        // Add custom products to the outputs list.
        if (!_customProducts.empty()) {
            for (const auto& product : _customProducts) {
                for (const auto& renderVar : product.renderVars) {
                    if (renderVar.writer != nullptr) {
                        aovShaders.push_back(renderVar.writer);
                    }
                    outputs.push_back(renderVar.output);
                }
            }
        }
        // finally add the user aov_shaders at the end so they can access all the AOVs
        aovShaders.insert(aovShaders.end(), _aovShaders.begin(), _aovShaders.end());

        // add the imager to the main driver
        AiNodeSetPtr(_mainDriver, str::input, imager);

        if (!outputs.empty()) {
            AiNodeSetArray(
                _renderDelegate->GetOptions(), str::outputs,
                AiArrayConvert(static_cast<uint32_t>(outputs.size()), 1, AI_TYPE_STRING, outputs.data()));
        }
        AiNodeSetArray(
            _renderDelegate->GetOptions(), str::light_path_expressions,
            lightPathExpressions.empty() ? AiArray(0, 1, AI_TYPE_STRING)
                                         : AiArrayConvert(
                                               static_cast<uint32_t>(lightPathExpressions.size()), 1,
                                               AI_TYPE_STRING, lightPathExpressions.data()));
        AiNodeSetArray(
            _renderDelegate->GetOptions(), str::aov_shaders,
            aovShaders.empty()
                ? AiArray(0, 1, AI_TYPE_NODE)
                : AiArrayConvert(static_cast<uint32_t>(aovShaders.size()), 1, AI_TYPE_NODE, aovShaders.data()));
        int bufferWidth = width;
        int bufferHeight = height;
        if (hasWindowNDC) {
            int regionMinX = AiNodeGetInt(options, str::region_min_x);
            int regionMaxX = AiNodeGetInt(options, str::region_max_x);
            int regionMinY = AiNodeGetInt(options, str::region_min_y);
            int regionMaxY = AiNodeGetInt(options, str::region_max_y);
            if (regionMaxX - regionMinX > 0 && regionMaxY - regionMinY > 0) {
                bufferWidth = regionMaxX - regionMinX + 1;
                bufferHeight = regionMaxY - regionMinY + 1;
            }                
        }
        clearBuffers(_renderBuffers, true, bufferWidth, bufferHeight);
    }

    // Check if hydra still has pending changes that will be processed in the next iteration.
    bool hasPendingChanges = _renderDelegate->HasPendingChanges(
        GetRenderIndex(), cameraId,
        {AiNodeGetFlt(currentCamera, str::shutter_start), AiNodeGetFlt(currentCamera, str::shutter_end)});
    
    // If we still have pending Hydra changes, we don't want to start / update the render just yet,
    // as we'll receive shortly another sync. In particular in the case of batch renders, this prevents
    // from rendering the final scene #2154
    const auto renderStatus = hasPendingChanges ? 
        HdArnoldRenderParam::Status::Converging : renderParam->UpdateRender();
    _isConverged = renderStatus != HdArnoldRenderParam::Status::Converging;

    // We need to set the converged status of the render buffers.
    if (!aovBindings.empty()) {
        // Clearing all AOVs if render was aborted.
        if (renderStatus == HdArnoldRenderParam::Status::Aborted) {
            clearBuffers(_renderBuffers, false, width, height);
        }
        for (auto& buffer : _renderBuffers) {
            if (buffer.second.buffer != nullptr) {
                buffer.second.buffer->SetConverged(_isConverged);
            }
        }
        // If the buffers are empty, we have to blit the data from the fallback buffers to OpenGL.
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
#if ARNOLD_VERSION_NUM >= 70405
    
    AiNodeResetParameter(_renderDelegate->GetOptions(), str::drivers);
    // With Arnold 7.4.5.0 and up, arnold converts the options outputs strings as render_output nodes.
    // Here we are destroying the filters & drivers, but we also have to destroy the render_outputs
    // in order to avoid possible crashes during interactive updates. This can go away when we directly
    // create render outputs here
    if (!_renderDelegate->GetProceduralParent()) {
        AtNodeIterator* nodeIter = AiUniverseGetNodeIterator(_renderDelegate->GetUniverse(), AI_NODE_RENDER_OUTPUT);
        while (!AiNodeIteratorFinished(nodeIter))
        {
           AtNode *node = AiNodeIteratorGetNext(nodeIter);
           AiNodeDestroy(node);
        }
        AiNodeIteratorDestroy(nodeIter);
    }
#endif


    for (auto& buffer : _renderBuffers) {
        if (buffer.second.filter != nullptr) {
            _renderDelegate->DestroyArnoldNode(buffer.second.filter);
        }
        if (buffer.second.writer != nullptr) {
            _renderDelegate->DestroyArnoldNode(buffer.second.writer);
        }
        if (buffer.second.reader != nullptr) {
            _renderDelegate->DestroyArnoldNode(buffer.second.reader);
        }
    }
    decltype(_renderBuffers){}.swap(_renderBuffers);
}

PXR_NAMESPACE_CLOSE_SCOPE
