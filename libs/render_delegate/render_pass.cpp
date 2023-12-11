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

GfRect2i _GetDataWindow(const HdRenderPassStateSharedPtr& renderPassState)
{
#if PXR_VERSION >= 2102
    const auto& framing = renderPassState->GetFraming();
    if (framing.IsValid()) {
        return framing.dataWindow;
    } else {
#endif
        // For applications that use the old viewport API instead of
        // the new camera framing API.
        const auto& vp = renderPassState->GetViewport();
        return GfRect2i(GfVec2i(0), int(vp[2]), int(vp[3]));
#if PXR_VERSION >= 2102
    }
#endif
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
    AtNode* filter = renderDelegate->CreateArnoldNode(AtString(filterType.c_str()), filterNameStr);
    
    if (filter == nullptr) {
        return filter;
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
        writer = renderDelegate->CreateArnoldNode(arnoldTypes.aovWrite, writerName);
        if (sourceName == "st" || sourceName == "uv") { // st and uv are written to the built-in UV
            reader = renderDelegate->CreateArnoldNode(str::utility, readerName);
            AiNodeSetStr(reader, str::color_mode, str::uv);
            AiNodeSetStr(reader, str::shade_mode, str::flat);
        } else {
            reader = renderDelegate->CreateArnoldNode(arnoldTypes.userData, readerName);
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
        TfStringPrintf("P VECTOR %s %s", AiNodeGetName(_closestFilter), AiNodeGetName(_mainDriver));
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
#if PXR_VERSION >= 2102
        // Ortho cameras were not supported in older versions of USD
        isOrtho =  camera->GetProjection() == HdCamera::Projection::Orthographic;
#endif
    }
    const auto dataWindow = _GetDataWindow(renderPassState);
    const auto width = static_cast<int>(dataWindow.GetWidth());
    const auto height = static_cast<int>(dataWindow.GetHeight());

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


    if (width != _width || height != _height) {
        // The render resolution has changed, we need to update the arnold options
        renderParam->Interrupt(true, false);
        _width = width;
        _height = height;
        auto* options = _renderDelegate->GetOptions();
        AiNodeSetInt(options, str::xres, _width);
        AiNodeSetInt(options, str::yres, _height);
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
            
            // Get the exact resolution, as returned by the render settings.
            // The one we received from the dataWindow might be affected by the 
            // dataWindowNDC
            GfVec2i renderSettingsRes = _renderDelegate->GetResolution();

            // we want the output render buffer to have a resolution equal to 
            // _width/_height. This means we need to adjust xres/yres, so that
            // region min/max corresponds to the render resolution
            float xDelta = windowNDC[2] - windowNDC[0]; // maxX - minX
            if (xDelta > AI_EPSILON) {
                float xInvDelta = 1.f / xDelta;
                // For batch renders, we want to ensure the arnold resolution is the one provided
                // by the render settings
                if (_renderDelegate->IsBatchContext() && renderSettingsRes[0] > 0)
                    AiNodeSetInt(options, str::xres, renderSettingsRes[0]);
                else {
                    AiNodeSetInt(options, str::xres, std::round(_width * (xInvDelta)));    
                }
                // Normalize windowNDC so that its delta is 1
                windowNDC[0] *= xInvDelta;
                windowNDC[2] *= xInvDelta;
            } else {
                AiNodeSetInt(options, str::xres, _width);
            }
            // we want region_max_x - region_min_x to be equal to _width - 1
            AiNodeSetInt(options, str::region_min_x, int(windowNDC[0] * _width));
            AiNodeSetInt(options, str::region_max_x, int(windowNDC[2] * _width) - 1);
            
            float yDelta = windowNDC[3] - windowNDC[1]; // maxY - minY
            if (yDelta > AI_EPSILON) {
                float yInvDelta = 1.f / yDelta;
                // For batch renders, we want to ensure the arnold resolution is the one provided
                // by the render settings
                if (_renderDelegate->IsBatchContext() && renderSettingsRes[1] > 0)
                    AiNodeSetInt(options, str::yres, renderSettingsRes[1]);
                else {
                    AiNodeSetInt(options, str::yres, std::round(_height * (yInvDelta)));
                }
                // Normalize windowNDC so that its delta is 1
                windowNDC[1] *= yInvDelta;    
                windowNDC[3] *= yInvDelta;

                // For interactive renders, need to adjust the pixel aspect ratio to match the window NDC
                if (!_renderDelegate->IsBatchContext()) {
                    pixelAspectRatio *= xDelta / yDelta;
                }
            
            } else {
                AiNodeSetInt(options, str::yres, _height);
            }

            // we want region_max_y - region_min_y to be equal to _height - 1
            AiNodeSetInt(options, str::region_min_y, int(windowNDC[1] * _height));
            AiNodeSetInt(options, str::region_max_y, int(windowNDC[3] * _height) - 1);
        } else {
            // the window was restored to defaults, we need to reset the region
            // attributes, as well as xres,yres, that could have been adjusted
            // in previous iterations
            AiNodeResetParameter(options, str::region_min_x);
            AiNodeResetParameter(options, str::region_min_y);
            AiNodeResetParameter(options, str::region_max_x);
            AiNodeResetParameter(options, str::region_max_y);
            AiNodeSetInt(options, str::xres, _width);
            AiNodeSetInt(options, str::yres, _height);
            _windowNDC = GfVec4f(0.f, 0.f, 1.f, 1.f);
        }
    }
    AiNodeSetFlt(options, str::pixel_aspect_ratio, pixelAspectRatio);

    auto checkShader = [&] (AtNode* shader, const AtString& paramName) {
        auto* options = _renderDelegate->GetOptions();
        if (shader != static_cast<AtNode*>(AiNodeGetPtr(options, paramName))) {
            renderParam->Interrupt(true, false);
            AiNodeSetPtr(options, paramName, shader);
        }
    };

    checkShader(_renderDelegate->GetBackground(GetRenderIndex()), str::background);
    checkShader(_renderDelegate->GetAtmosphere(GetRenderIndex()), str::atmosphere);

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
    if (subdivDicingCamera)
        AiNodeSetPtr(options, str::subdiv_dicing_camera, (void*)subdivDicingCamera);
    else
        AiNodeResetParameter(options, str::subdiv_dicing_camera);

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

    // Delegate Render Products are only introduced in Houdini 18.5, which is 20.8 that has USD_DO_NOT_BLIT always set.
#ifndef USD_DO_NOT_BLIT
    if (aovBindings.empty()) {
        // We are first checking if the right storage pointer is set on the driver.
        // If not, then we need to reset the aov setup and set the outputs definition on the driver.
        // If it's the same pointer, we still need to check the dimensions, if they don't match the global dimensions,
        // then reallocate those render buffers.
        // If USD has the newer compositor class, we can allocate float buffers for the color, otherwise we need to
        // stick to UNorm8.
        if (!_usingFallbackBuffers) {
            renderParam->Interrupt(true, false);
            AiNodeSetArray(_renderDelegate->GetOptions(), str::outputs, AiArrayCopy(_fallbackOutputs));
            AiNodeSetArray(_renderDelegate->GetOptions(), str::aov_shaders, AiArrayCopy(_fallbackAovShaders));
            _usingFallbackBuffers = true;
            AiNodeSetPtr(_mainDriver, str::aov_pointer, &_fallbackBuffers);
            AiNodeSetPtr(_mainDriver, str::color_pointer, &_fallbackColor);
            AiNodeSetPtr(_mainDriver, str::depth_pointer, &_fallbackDepth);
            AiNodeSetPtr(_mainDriver, str::id_pointer, &_fallbackPrimId);
        }
        if (_fallbackColor.GetWidth() != static_cast<unsigned int>(_width) ||
            _fallbackColor.GetHeight() != static_cast<unsigned int>(_height)) {
            renderParam->Interrupt(true, false);
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
        const auto& delegateRenderProducts = _renderDelegate->GetDelegateRenderProducts();
        if (_RenderBuffersChanged(aovBindings) || (!delegateRenderProducts.empty() && _customProducts.empty()) ||
            _usingFallbackBuffers || updateAovs || updateImagers) {
            _usingFallbackBuffers = false;
            renderParam->Interrupt();
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
            // - depth -> P VECTOR for remapping point to depth using the projection matrices closest filter by default
            // - primId -> ID UINT closest filter by default
            // - everything else -> aovName RGB closest filter by default
            // We are using box filter for the color and closest for everything else.
            const auto* boxName = AiNodeGetName(_defaultFilter);
            const auto* closestName = AiNodeGetName(_closestFilter);
            const auto* mainDriverName = AiNodeGetName(_mainDriver);
            int bufferIndex = 0;
            int filterIndex = 0;
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
                    output = AtString{TfStringPrintf("P VECTOR %s %s", filterGeoName, mainDriverName).c_str()};
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

                    // Creating a separate driver for each aov.
                    AtString driverNameStr = _renderDelegate->GetLocalNodeName(
                        AtString{TfStringPrintf("HdArnoldRenderPass_aov_driver_%d", ++bufferIndex).c_str()});

                    buffer.driver = _renderDelegate->CreateArnoldNode(str::HdArnoldDriverAOV,
                        driverNameStr);
                    
                    AiNodeSetPtr(buffer.driver, str::aov_pointer, buffer.buffer);

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
                        buffer.writer = _renderDelegate->CreateArnoldNode(arnoldTypes.aovWrite,
                            writerName);
                        if (sourceName == "st" || sourceName == "uv") { // st and uv are written to the built-in UV
                            buffer.reader = _renderDelegate->CreateArnoldNode(str::utility,
                                readerName);
                            AiNodeSetStr(buffer.reader, str::color_mode, str::uv);
                            AiNodeSetStr(buffer.reader, str::shade_mode, str::flat);
                        } else {
                            buffer.reader = _renderDelegate->CreateArnoldNode(AtString(arnoldTypes.userData.c_str()),
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
                    // If this driver is meant for one of the cryptomatte AOVs, it will be filled with the 
                    // cryptomatte metadatas through the user data "custom_attributes". We want to store 
                    // the driver node names in the render delegate, so that we can lookup this user data
                    // during GetRenderStats
                    if (binding.aovName == str::t_crypto_asset || 
                        binding.aovName == str::t_crypto_material ||
                        binding.aovName == str::t_crypto_object)
                        _renderDelegate->RegisterCryptomatteDriver(driverNameStr);
                    
                    output = AtString{
                        TfStringPrintf(
                            "%s %s %s %s", aovName, arnoldTypes.outputString, filterName, AiNodeGetName(buffer.driver))
                            .c_str()};

                    if (!strcmp(aovName, "RGBA")) {
                        AiNodeSetPtr(buffer.driver, str::input, imager);
                    }

                }
                outputs.push_back(output);
            }
            // We haven't initialized the custom products yet.
            // At the moment this won't work if delegate render products are set interactively, as this is only meant to
            // override the output driver for batch renders. In Solaris, 
            // delegate render products are only set when rendering in husk.
            if (!delegateRenderProducts.empty() && _customProducts.empty()) {
                _customProducts.reserve(delegateRenderProducts.size());
                // Get an eventual output override string. We only want to use it if no outputs 
                // were added above with hydra drives, since they will render to the same filename
                // and we don't want several drivers writing to the same image
                const std::string &outputOverride = _renderDelegate->GetOutputOverride();
                bool hasOutputOverride = (!outputOverride.empty()) && outputs.empty();

                for (const auto& product : delegateRenderProducts) {
                    CustomProduct customProduct;
                    if (product.renderVars.empty()) {
                        continue;
                    }
                    const AtString customDriverName =
                        AtString{TfStringPrintf("HdArnoldRenderPass_driver_%s_%d", product.productType.GetText(), ++bufferIndex).c_str()};
                    
                    customProduct.driver = _renderDelegate->CreateArnoldNode(AtString(product.productType.GetText()),
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

                    // FIXME do we still need to do a special case for deep exrs ?
                    constexpr float defaultTolerance = 0.01f;
                    constexpr bool defaultEnableFiltering = true;
                    constexpr bool defaultHalfPrecision = false;
                    const auto numRenderVars = static_cast<uint32_t>(product.renderVars.size());
                    auto* toleranceArray = AiArrayAllocate(numRenderVars, 1, AI_TYPE_FLOAT);
                    auto* tolerance = static_cast<float*>(AiArrayMap(toleranceArray));
                    auto* enableFilteringArray = AiArrayAllocate(numRenderVars, 1, AI_TYPE_BOOLEAN);
                    auto* enableFiltering = static_cast<bool*>(AiArrayMap(enableFilteringArray));
                    auto* halfPrecisionArray = AiArrayAllocate(numRenderVars, 1, AI_TYPE_BOOLEAN);
                    auto* halfPrecision = static_cast<bool*>(AiArrayMap(halfPrecisionArray));
                    for (const auto& renderVar : product.renderVars) {
                        CustomRenderVar customRenderVar;
                        *tolerance =
                            _GetOptionalSetting<float>(renderVar.settings, _tokens->tolerance, defaultTolerance);
                        *enableFiltering = _GetOptionalSetting<bool>(
                            renderVar.settings, _tokens->enableFiltering, defaultEnableFiltering);
                        *halfPrecision =
                            _GetOptionalSetting<bool>(renderVar.settings, _tokens->halfPrecision, defaultHalfPrecision);
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
                            // Check if the AOV has a specific filter
                            const auto arnoldAovFilterName = _GetOptionalSetting<std::string>(renderVar.settings, _tokens->aovSettingFilter, "");
                            AtNode *aovFilterNode = arnoldAovFilterName.empty() ? nullptr : _CreateFilter(_renderDelegate, renderVar.settings, ++filterIndex);
                            customRenderVar.output =
                                AtString{TfStringPrintf(
                                             arnoldTypes.isHalf ? "%s %s %s %s HALF":  "%s %s %s %s", aovName.c_str(), arnoldTypes.outputString, aovFilterNode ? AiNodeGetName(aovFilterNode) : filterName,
                                             customDriverName.c_str())
                                             .c_str()};
                        }
                        tolerance += 1;
                        enableFiltering += 1;
                        halfPrecision += 1;
                        customProduct.renderVars.push_back(customRenderVar);
                    }
                    AiArrayUnmap(toleranceArray);
                    AiArrayUnmap(enableFilteringArray);
                    AiArrayUnmap(halfPrecisionArray);

                    // FIXME do we still need to do a special case for deep exr or should we generalize this ? #1422
                    if (AiNodeIs(customProduct.driver, str::driver_deepexr)) {
                        AiNodeSetArray(customProduct.driver, str::layer_tolerance, toleranceArray);
                        AiNodeSetArray(customProduct.driver, str::layer_enable_filtering, enableFilteringArray);
                        AiNodeSetArray(customProduct.driver, str::layer_half_precision, halfPrecisionArray);
                    }
                    _customProducts.push_back(std::move(customProduct));
                }

                if (_customProducts.empty()) {
                    // if we didn't manage to create any custom product, we want
                    // the render delegate to clear its list. Otherwise the tests above 
                    // (!delegateRenderProducts.empty() && _customProducts.empty())
                    // will keep triggering changes and the render will start over and over
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
            clearBuffers(_renderBuffers);
        }
#ifndef USD_DO_NOT_BLIT
    }
#endif

    // We skip an iteration step if the render delegate tells us to do so, this is the easiest way to force
    // a sync step before calling the render function. Currently, this is used to trigger light linking updates.
    const auto shouldSkipIteration = _renderDelegate->ShouldSkipIteration(
        GetRenderIndex(),
        {AiNodeGetFlt(currentCamera, str::shutter_start), AiNodeGetFlt(currentCamera, str::shutter_end)});
    const auto renderStatus = shouldSkipIteration ? HdArnoldRenderParam::Status::Converging : renderParam->Render();
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
            _renderDelegate->DestroyArnoldNode(buffer.second.filter);
        }
        if (buffer.second.driver != nullptr) {
            _renderDelegate->DestroyArnoldNode(buffer.second.driver);
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
