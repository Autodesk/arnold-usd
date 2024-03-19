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
#include "render_delegate.h"

#include <pxr/base/tf/getenv.h>

#include <pxr/imaging/hd/bprim.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/extComputation.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/tokens.h>

#include <common_utils.h>
#include <constant_strings.h>
#include <shape_utils.h>

#include "basis_curves.h"
#include "camera.h"
#include "config.h"
#include "instancer.h"
#include "light.h"
#include "mesh.h"
#include "native_rprim.h"
#include "node_graph.h"
#include "nodes/nodes.h"
#include "openvdb_asset.h"
#include "options.h"
#include "points.h"
#include "procedural_custom.h"
#include "render_buffer.h"
#include "render_pass.h"
#include "volume.h"
#include <cctype>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (arnold)
    ((aovDriverFormat, "driver:parameters:aov:format"))
    ((aovFormat, "arnold:format"))
    (ArnoldOptions)
    (openvdbAsset)
    ((arnoldGlobal, "arnold:global:"))
    ((arnoldDriver, "arnold:driver"))
    ((arnoldNamespace, "arnold:"))
    (batchCommandLine)
    (percentDone)
    (totalClockTime)
    (renderProgressAnnotation)
    (delegateRenderProducts)
    (orderedVars)
    ((aovSettings, "aovDescriptor.aovSettings"))
    (productType)
    (productName)
    (pixelAspectRatio)
    (driver_exr)
    (sourceType)
    (sourceName)
    (dataType)
    ((format, "aovDescriptor.format"))
    ((clearValue, "aovDescriptor.clearValue"))
    ((multiSampled, "aovDescriptor.multiSampled"))
    ((aovName, "driver:parameters:aov:name"))
    (deep)
    (raw)
    (instantaneousShutter)
    ((aovShadersArray, "aov_shaders:i"))
    (GeometryLight)
    (dataWindowNDC)
    (resolution)
    // The following tokens are also defined in read_options.cpp, we need them
    // here for the conversion from TfToken to HdFormat, while in read_options they
    // are used for the conversion of HdFormat to TfToken.
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

#define PXR_VERSION_STR \
    ARNOLD_XSTR(PXR_MAJOR_VERSION) "." ARNOLD_XSTR(PXR_MINOR_VERSION) "." ARNOLD_XSTR(PXR_PATCH_VERSION)

namespace {

const HdFormat _GetHdFormatFromToken(const TfToken& token)
{
    if (token == _tokens->uint8) {
        return HdFormatUNorm8;
    } else if (token == _tokens->color2u8) {
        return HdFormatUNorm8Vec2;
    } else if (token == _tokens->color3u8) {
        return HdFormatUNorm8Vec3;
    } else if (token == _tokens->color4u8) {
        return HdFormatUNorm8Vec4;
    } else if (token == _tokens->int8) {
        return HdFormatSNorm8;
    } else if (token == _tokens->color2i8) {
        return HdFormatSNorm8Vec2;
    } else if (token == _tokens->color3i8) {
        return HdFormatSNorm8Vec3;
    } else if (token == _tokens->color4i8) {
        return HdFormatSNorm8Vec4;
    } else if (token == _tokens->half) {
        return HdFormatFloat16;
    } else if (token == _tokens->half2 || token == _tokens->color2h) {
        return HdFormatFloat16Vec2;
    } else if (token == _tokens->half3 || token == _tokens->color3h) {
        return HdFormatFloat16Vec3;
    } else if (token == _tokens->half4 || token == _tokens->color4h) {
        return HdFormatFloat16Vec4;
    } else if (token == _tokens->_float) {
        return HdFormatFloat32;
    } else if (token == _tokens->float2 || token == _tokens->color2f) {
        return HdFormatFloat32Vec2;
    } else if (token == _tokens->float3 || token == _tokens->color3f) {
        return HdFormatFloat32Vec3;
    } else if (token == _tokens->float4 || token == _tokens->color4f) {
        return HdFormatFloat32Vec4;
    } else if (token == _tokens->_int) {
        return HdFormatInt32;
    } else if (token == _tokens->int2) {
        return HdFormatInt32Vec2;
    } else if (token == _tokens->int3) {
        return HdFormatInt32Vec3;
    } else if (token == _tokens->int4) {
        return HdFormatInt32Vec4;
    } else {
        return HdFormatInvalid;
    }
}

VtValue _GetNodeParamValue(AtNode* node, const AtParamEntry* pentry)
{
    if (Ai_unlikely(pentry == nullptr)) {
        return {};
    }
    const auto ptype = AiParamGetType(pentry);
    if (ptype == AI_TYPE_INT) {
        return VtValue(AiNodeGetInt(node, AiParamGetName(pentry)));
    } else if (ptype == AI_TYPE_FLOAT) {
        return VtValue(AiNodeGetFlt(node, AiParamGetName(pentry)));
    } else if (ptype == AI_TYPE_BOOLEAN) {
        return VtValue(AiNodeGetBool(node, AiParamGetName(pentry)));
    } else if (ptype == AI_TYPE_STRING || ptype == AI_TYPE_ENUM) {
        return VtValue(std::string(AiNodeGetStr(node, AiParamGetName(pentry))));
    }
    return {};
}

void _SetNodeParam(AtNode* node, const TfToken& key, const VtValue& value)
{
    // Some applications might send integers instead of booleans.
    if (value.IsHolding<int>()) {
        const auto* nodeEntry = AiNodeGetNodeEntry(node);
        auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(key.GetText()));
        if (paramEntry != nullptr) {
            const auto paramType = AiParamGetType(paramEntry);
            if (paramType == AI_TYPE_INT) {
                AiNodeSetInt(node, AtString(key.GetText()), value.UncheckedGet<int>());
            } else if (paramType == AI_TYPE_BOOLEAN) {
                AiNodeSetBool(node, AtString(key.GetText()), value.UncheckedGet<int>() != 0);
            }
        }
        // Or longs.
    } else if (value.IsHolding<long>()) {
        const auto* nodeEntry = AiNodeGetNodeEntry(node);
        auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(key.GetText()));
        if (paramEntry != nullptr) {
            const auto paramType = AiParamGetType(paramEntry);
            if (paramType == AI_TYPE_INT) {
                AiNodeSetInt(node, AtString(key.GetText()), static_cast<int>(value.UncheckedGet<long>()));
            } else if (paramType == AI_TYPE_BOOLEAN) {
                AiNodeSetBool(node, AtString(key.GetText()), value.UncheckedGet<long>() != 0);
            }
        }
        // Or long longs.
    } else if (value.IsHolding<long long>()) {
        const auto* nodeEntry = AiNodeGetNodeEntry(node);
        auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(key.GetText()));
        if (paramEntry != nullptr) {
            const auto paramType = AiParamGetType(paramEntry);
            if (paramType == AI_TYPE_INT) {
                AiNodeSetInt(node, AtString(key.GetText()), static_cast<int>(value.UncheckedGet<long long>()));
            } else if (paramType == AI_TYPE_BOOLEAN) {
                AiNodeSetBool(node, AtString(key.GetText()), value.UncheckedGet<long long>() != 0);
            }
        }
    } else if (value.IsHolding<float>()) {
        AiNodeSetFlt(node, AtString(key.GetText()), value.UncheckedGet<float>());
    } else if (value.IsHolding<double>()) {
        AiNodeSetFlt(node, AtString(key.GetText()), static_cast<float>(value.UncheckedGet<double>()));
    } else if (value.IsHolding<bool>()) {
        AiNodeSetBool(node, AtString(key.GetText()), value.UncheckedGet<bool>());
    } else if (value.IsHolding<std::string>()) {
        AiNodeSetStr(node, AtString(key.GetText()), AtString(value.UncheckedGet<std::string>().c_str()));
    } else if (value.IsHolding<TfToken>()) {
        AiNodeSetStr(node, AtString(key.GetText()), AtString(value.UncheckedGet<TfToken>().GetText()));
    }
}

inline const TfTokenVector& _SupportedSprimTypes()
{
    static const TfTokenVector r{HdPrimTypeTokens->camera,        HdPrimTypeTokens->material,
                                 HdPrimTypeTokens->distantLight,  HdPrimTypeTokens->sphereLight,
                                 HdPrimTypeTokens->diskLight,     HdPrimTypeTokens->rectLight,
                                 HdPrimTypeTokens->cylinderLight, HdPrimTypeTokens->domeLight,
                                 _tokens->GeometryLight, _tokens->ArnoldOptions,
                                 HdPrimTypeTokens->extComputation
                                 /*HdPrimTypeTokens->simpleLight*/};
    return r;
}

inline const TfTokenVector& _SupportedBprimTypes()
{
    static const TfTokenVector r{HdPrimTypeTokens->renderBuffer, _tokens->openvdbAsset};
    return r;
}

struct SupportedRenderSetting {
    /// Constructor with no default value.
    SupportedRenderSetting(const char* _label) : label(_label) {}

    /// Constructor with a default value.
    template <typename T>
    SupportedRenderSetting(const char* _label, const T& _defaultValue) : label(_label), defaultValue(_defaultValue)
    {
    }

    TfToken label;
    VtValue defaultValue;
};

using SupportedRenderSettings = std::vector<std::pair<TfToken, SupportedRenderSetting>>;
using VtStringArray = VtArray<std::string>;
const SupportedRenderSettings& _GetSupportedRenderSettings()
{
    static const auto& config = HdArnoldConfig::GetInstance();
    static const SupportedRenderSettings data{
        // Global settings to control rendering
        {str::t_enable_progressive_render, {"Enable Progressive Render", config.enable_progressive_render}},
        {str::t_progressive_min_AA_samples,
         {"Progressive Render Minimum AA Samples", config.progressive_min_AA_samples}},
        {str::t_enable_adaptive_sampling, {"Enable Adaptive Sampling", config.enable_adaptive_sampling}},
#ifndef __APPLE__
        {str::t_enable_gpu_rendering, {"Enable GPU Rendering", config.enable_gpu_rendering}},
#endif
        {str::t_interactive_target_fps, {"Target FPS for Interactive Rendering", config.interactive_target_fps}},
        {str::t_interactive_target_fps_min,
         {"Minimum Target FPS for Interactive Rendering", config.interactive_target_fps_min}},
        {str::t_interactive_fps_min, {"Minimum FPS for Interactive Rendering", config.interactive_fps_min}},
        // Threading settings
        {str::t_threads, {"Number of Threads", config.threads}},
        // Sampling settings
        {str::t_AA_samples, {"AA Samples", config.AA_samples}},
        {str::t_AA_samples_max, {"AA Samples Max"}},
        {str::t_GI_diffuse_samples, {"Diffuse Samples", config.GI_diffuse_samples}},
        {str::t_GI_specular_samples, {"Specular Samples", config.GI_specular_samples}},
        {str::t_GI_transmission_samples, {"Transmission Samples", config.GI_transmission_samples}},
        {str::t_GI_sss_samples, {"SubSurface Scattering Samples", config.GI_sss_samples}},
        {str::t_GI_volume_samples, {"Volume Samples", config.GI_volume_samples}},
        // Depth settings
        {str::t_auto_transparency_depth, {"Auto Transparency Depth"}},
        {str::t_GI_diffuse_depth, {"Diffuse Depth", config.GI_diffuse_depth}},
        {str::t_GI_specular_depth, {"Specular Depth", config.GI_specular_depth}},
        {str::t_GI_transmission_depth, {"Transmission Depth", config.GI_transmission_depth}},
        {str::t_GI_volume_depth, {"Volume Depth"}},
        {str::t_GI_total_depth, {"Total Depth"}},
        // Ignore settings
        {str::t_abort_on_error, {"Abort On Error", config.abort_on_error}},
        {str::t_ignore_textures, {"Ignore Textures"}},
        {str::t_ignore_shaders, {"Ignore Shaders"}},
        {str::t_ignore_atmosphere, {"Ignore Atmosphere"}},
        {str::t_ignore_lights, {"Ignore Lights"}},
        {str::t_ignore_shadows, {"Ignore Shadows"}},
        {str::t_ignore_subdivision, {"Ignore Subdivision"}},
        {str::t_ignore_displacement, {"Ignore Displacement"}},
        {str::t_ignore_bump, {"Ignore Bump"}},
        {str::t_ignore_motion, {"Ignore Motion"}},
        {str::t_ignore_motion_blur, {"Ignore Motion Blur"}},
        {str::t_ignore_dof, {"Ignore Depth of Field"}},
        {str::t_ignore_smoothing, {"Ignore Smoothing"}},
        {str::t_ignore_sss, {"Ignore SubSurface Scattering"}},
        {str::t_ignore_operators, {"Ignore Operators"}},
        // Log Settings
        {str::t_log_verbosity, {"Log Verbosity (0-5)", config.log_verbosity}},
        {str::t_log_file, {"Log File Path", config.log_file}},
        // Profiling Settings
        {str::t_profile_file, {"File Output for Profiling", config.profile_file}},
        // Search paths
        {str::t_texture_searchpath, {"Texture search path.", config.texture_searchpath}},
        {str::t_plugin_searchpath, {"Plugin search path.", config.plugin_searchpath}},
        {str::t_procedural_searchpath, {"Procedural search path.", config.procedural_searchpath}},
        {str::t_osl_includepath, {"OSL include path.", config.osl_includepath}},

        {str::t_subdiv_dicing_camera, {"Subdiv Dicing Camera", std::string{}}},
        {str::t_subdiv_frustum_culling, {"Subdiv Frustum Culling"}},
        {str::t_subdiv_frustum_padding, {"Subdiv Frustum Padding"}},

        {str::t_background, {"Path to the background node graph.", std::string{}}},
        {str::t_atmosphere, {"Path to the atmosphere node graph.", std::string{}}},
        {str::t_aov_shaders, {"Path to the aov_shaders node graph.", std::string{}}},
        {str::t_imager, {"Path to the imagers node graph.", std::string{}}},
        {str::t_texture_auto_generate_tx, {"Auto-generate Textures to TX", config.auto_generate_tx}},
    };
    return data;
}

int _GetLogFlagsFromVerbosity(int verbosity)
{
    if (verbosity <= 0) {
        return 0;
    }
    if (verbosity >= 5) {
        return AI_LOG_ALL & ~AI_LOG_COLOR;
    }

    int flags = AI_LOG_ERRORS | AI_LOG_TIMESTAMP | AI_LOG_MEMORY | AI_LOG_BACKTRACE;

    if (verbosity >= 2) {
        flags |= AI_LOG_WARNINGS;
        if (verbosity >= 3) {
            // Don't want progress without info, as otherwise it never prints a
            // "render done" message!
            flags |= AI_LOG_INFO | AI_LOG_PROGRESS;
            if (verbosity >= 4) {
                flags |= AI_LOG_STATS | AI_LOG_PLUGINS;
            }
        }
    }
    return flags;
}

template <typename F>
void _CheckForBoolValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<bool>()) {
        f(value.UncheckedGet<bool>());
    } else if (value.IsHolding<int>()) {
        f(value.UncheckedGet<int>() != 0);
    } else if (value.IsHolding<long>()) {
        f(value.UncheckedGet<long>() != 0);
    } else if (value.IsHolding<long long>()) {
        f(value.UncheckedGet<long long>() != 0);
    }
}

template <typename F>
void _CheckForIntValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<int>()) {
        f(value.UncheckedGet<int>());
    } else if (value.IsHolding<long>()) {
        f(static_cast<int>(value.UncheckedGet<long>()));
    } else if (value.IsHolding<long long>()) {
        f(static_cast<int>(value.UncheckedGet<long long>()));
    }
}

template <typename F>
void _CheckForFloatValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<float>()) {
        f(value.UncheckedGet<float>());
    } else if (value.IsHolding<double>()) {
        f(static_cast<float>(value.UncheckedGet<double>()));
    } else if (value.IsHolding<GfHalf>()) {
        f(value.UncheckedGet<GfHalf>());
    }
}

void _RemoveArnoldGlobalPrefix(const TfToken& key, TfToken& key_new)
{
    if (TfStringStartsWith(key, _tokens->arnoldGlobal))
        key_new = TfToken{key.GetText() + _tokens->arnoldGlobal.size()};
    else if (TfStringStartsWith(key, _tokens->arnoldNamespace))
        key_new = TfToken{key.GetText() + _tokens->arnoldNamespace.size()};
    else 
        key_new = key;
}

} // namespace

std::mutex HdArnoldRenderDelegate::_mutexResourceRegistry;
std::atomic_int HdArnoldRenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdArnoldRenderDelegate::_resourceRegistry;

AtNode* HydraArnoldAPI::CreateArnoldNode(const char* type, const char* name)
{
    return _renderDelegate->CreateArnoldNode(AtString(type), AtString(name));
}
const AtNode* HydraArnoldAPI::GetProceduralParent() const
{
    return _renderDelegate->GetProceduralParent();
}

void HydraArnoldAPI::AddNodeName(const std::string &name, AtNode *node)
{
    _renderDelegate->AddNodeName(name, node);
}
AtNode* HydraArnoldAPI::LookupTargetNode(const char* targetName, const AtNode* source, ConnectionType c)
{
    return _renderDelegate->LookupNode(targetName, true);
}
const AtString& HydraArnoldAPI::GetPxrMtlxPath() 
{
    return _renderDelegate->GetPxrMtlxPath();
}

HdArnoldRenderDelegate::HdArnoldRenderDelegate(bool isBatch, const TfToken &context, AtUniverse *universe=nullptr) : 
    _apiAdapter(this),
    _isBatch(isBatch), 
    _context(context),
    _universe(universe),
    _procParent(nullptr),
    _renderDelegateOwnsUniverse(universe==nullptr)
{    

    _lightLinkingChanged.store(false, std::memory_order_release);
    _meshLightsChanged.store(false, std::memory_order_release);
    _id = SdfPath(TfToken(TfStringPrintf("/HdArnoldRenderDelegate_%p", this)));
    // We first need to check if arnold has already been initialized.
    // If not, we need to call AiBegin, and the destructor on we'll call AiEnd
#if ARNOLD_VERSION_NUM >= 70100
    _isArnoldActive = AiArnoldIsActive();
#else
    _isArnoldActive = AiUniverseIsActive();
#endif
    if (_isBatch) {
#if ARNOLD_VERSION_NUM >= 70104
        // Ensure that the ADP dialog box will not pop up and hang the application
        AiADPDisableDialogWindow();
        AiErrorReportingSetEnabled(false);
#endif
    }
    if (!_isArnoldActive) {
        AiADPAddProductMetadata(AI_ADP_PLUGINNAME, AtString{"arnold-usd"});
        AiADPAddProductMetadata(AI_ADP_PLUGINVERSION, AtString{AI_VERSION});
        AiADPAddProductMetadata(AI_ADP_HOSTNAME, AtString{"Hydra"});
        AiADPAddProductMetadata(AI_ADP_HOSTVERSION, AtString{PXR_VERSION_STR});
        // TODO(pal): We need to investigate if it's safe to set session to AI_SESSION_BATCH when rendering in husk for
        //  example. ie. is husk creating a separate render delegate for each frame, or syncs the changes?
        AiBegin(AI_SESSION_INTERACTIVE);
    }
    _supportedRprimTypes = {HdPrimTypeTokens->mesh, HdPrimTypeTokens->volume, HdPrimTypeTokens->points,
                            HdPrimTypeTokens->basisCurves, str::t_procedural_custom};
    if (_mask & AI_NODE_SHAPE) {
        auto* shapeIter = AiUniverseGetNodeEntryIterator(AI_NODE_SHAPE);
        while (!AiNodeEntryIteratorFinished(shapeIter)) {
            const auto* nodeEntry = AiNodeEntryIteratorGetNext(shapeIter);
            TfToken rprimType{ArnoldUsdMakeCamelCase(TfStringPrintf("Arnold_%s", AiNodeEntryGetName(nodeEntry)))};
            _supportedRprimTypes.push_back(rprimType);
            _nativeRprimTypes.insert({rprimType, AiNodeEntryGetNameAtString(nodeEntry)});

            NativeRprimParamList paramList;
            auto* paramIter = AiNodeEntryGetParamIterator(nodeEntry);
            while (!AiParamIteratorFinished(paramIter)) {
                const auto* param = AiParamIteratorGetNext(paramIter);
                const auto paramName = AiParamGetName(param);
                if (ArnoldUsdIgnoreParameter(paramName)) {
                    continue;
                }
    #if PXR_VERSION >= 2011
                paramList.emplace(TfToken{TfStringPrintf("arnold:%s", paramName.c_str())}, param);
    #else
                paramList.emplace_back(TfToken{TfStringPrintf("arnold:%s", paramName.c_str())}, param);
    #endif
            }

            _nativeRprimParams.emplace(AiNodeEntryGetNameAtString(nodeEntry), std::move(paramList));
            AiParamIteratorDestroy(paramIter);
        }
    }
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
    if (_counterResourceRegistry.fetch_add(1) == 0) {
        _resourceRegistry.reset(new HdResourceRegistry());
    }

    const auto& config = HdArnoldConfig::GetInstance();
    if (config.log_flags_console >= 0) {
        _ignoreVerbosityLogFlags = true;
        #if ARNOLD_VERSION_NUM < 70100
            AiMsgSetConsoleFlags(GetRenderSession(), config.log_flags_console);
        #else
            AiMsgSetConsoleFlags(_universe, config.log_flags_console);
        #endif
    } else {
        #if ARNOLD_VERSION_NUM < 70100
            AiMsgSetConsoleFlags(GetRenderSession(), config.log_flags_console);
        #else
            AiMsgSetConsoleFlags(_universe, _verbosityLogFlags);
        #endif
    }
    if (config.log_flags_file >= 0) {
        #if ARNOLD_VERSION_NUM < 70100
            AiMsgSetLogFileFlags(GetRenderSession(), config.log_flags_file);
        #else
            AiMsgSetLogFileFlags(_universe, config.log_flags_file);
        #endif
    }
    hdArnoldInstallNodes();
    // Check the USD environment variable for custom Materialx node definitions.
    // We need to use this to pass it on to Arnold's MaterialX
    const char *pxrMtlxPath = std::getenv("PXR_MTLX_STDLIB_SEARCH_PATHS");
    if (pxrMtlxPath) {
        _pxrMtlxPath = AtString(pxrMtlxPath);
    }

    if (_renderDelegateOwnsUniverse) {
        _universe = AiUniverse();
        _renderSession = AiRenderSession(_universe, AI_SESSION_INTERACTIVE);
    }

    _renderParam.reset(new HdArnoldRenderParam(this));
    // To set the default value.
    _fps = _renderParam->GetFPS();
    _options = AiUniverseGetOptions(_universe);
    if (_renderDelegateOwnsUniverse) {
        for (const auto& o : _GetSupportedRenderSettings()) {
            _SetRenderSetting(o.first, o.second.defaultValue);
        }
        AiRenderSetHintStr(
            GetRenderSession(), str::render_context, AtString(_context.GetText()));

        // We need access to both beauty and P at the same time.
        if (_isBatch) {
            AiRenderSetHintBool(GetRenderSession(), str::progressive, false);
            AiNodeSetBool(_options, str::enable_progressive_render, false);
        } else {
            AiRenderSetHintBool(GetRenderSession(), str::progressive_show_all_outputs, true);
        }
    }
    
    _fallbackShader = CreateArnoldNode(str::standard_surface, 
        AtString("_fallbackShader"));
    
    AtNode *userDataReader = CreateArnoldNode(str::user_data_rgb,
        AtString("_fallbackShader_userDataReader"));
    
    AiNodeSetStr(userDataReader, str::attribute, str::displayColor);
    AiNodeSetRGB(userDataReader, str::_default, 1.0f, 1.0f, 1.0f);
    AiNodeLink(userDataReader, str::base_color, _fallbackShader);

    _fallbackVolumeShader = CreateArnoldNode(str::standard_volume,
        AtString("_fallbackVolume"));

}

HdArnoldRenderDelegate::~HdArnoldRenderDelegate()
{
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
    if (_counterResourceRegistry.fetch_sub(1) == 1) {
        _resourceRegistry.reset();
    }
    _renderParam->Interrupt();
    if (_renderDelegateOwnsUniverse) {
        AiRenderSessionDestroy(GetRenderSession());
        hdArnoldUninstallNodes();
        AiUniverseDestroy(_universe);
        // We must end the arnold session, only if we created it during the constructor.
        // Otherwise we could be destroying a session that is being used elsewhere
        if (!_isArnoldActive)
        AiEnd();
    }

}

HdRenderParam* HdArnoldRenderDelegate::GetRenderParam() const { return _renderParam.get(); }

void HdArnoldRenderDelegate::CommitResources(HdChangeTracker* tracker) {}

const TfTokenVector& HdArnoldRenderDelegate::GetSupportedRprimTypes() const { return _supportedRprimTypes; }

const TfTokenVector& HdArnoldRenderDelegate::GetSupportedSprimTypes() const { return _SupportedSprimTypes(); }

const TfTokenVector& HdArnoldRenderDelegate::GetSupportedBprimTypes() const { return _SupportedBprimTypes(); }

void HdArnoldRenderDelegate::_SetRenderSetting(const TfToken& _key, const VtValue& _value)
{    
    // function to get or create the color manager and set it on the options node
    auto getOrCreateColorManager = [](HdArnoldRenderDelegate *renderDelegate, AtNode* options) -> AtNode* {
        AtNode* colorManager = static_cast<AtNode*>(AiNodeGetPtr(options, str::color_manager));
        if (colorManager == nullptr) {
            const char *ocio_path = std::getenv("OCIO");
            if (ocio_path) {
                colorManager = renderDelegate->CreateArnoldNode(str::color_manager_ocio, 
                    str::color_manager_ocio);
                AiNodeSetPtr(options, str::color_manager, colorManager);
                AiNodeSetStr(colorManager, str::config, AtString(ocio_path));
            }
            else
                // use the default color manager
                colorManager = renderDelegate->LookupNode("ai_default_color_manager_ocio");
        }
        return colorManager;
    };

    // Special setting that describes custom output, like deep AOVs or other arnold drivers #1422.
    if (_key == _tokens->delegateRenderProducts) {
        _ParseDelegateRenderProducts(_value);
        return;
    }
    TfToken key;
    _RemoveArnoldGlobalPrefix(_key, key);

    // Currently usdview can return double for floats, so until it's fixed
    // we have to convert doubles to float.
    auto value = _value.IsHolding<double>() ? VtValue(static_cast<float>(_value.UncheckedGet<double>())) : _value;
    // Certain applications might pass boolean values via ints or longs.
    if (key == str::t_enable_gpu_rendering) {
        _CheckForBoolValue(value, [&](const bool b) {
            AiNodeSetStr(_options, str::render_device, b ? str::GPU : str::CPU);
            AiDeviceAutoSelect(GetRenderSession());
        });
    } else if (key == str::t_log_verbosity) {
        if (value.IsHolding<int>()) {
            _verbosityLogFlags = _GetLogFlagsFromVerbosity(value.UncheckedGet<int>());
            if (!_ignoreVerbosityLogFlags) {
                #if ARNOLD_VERSION_NUM < 70100
                    AiMsgSetConsoleFlags(GetRenderSession(), _verbosityLogFlags);
                #else
                    AiMsgSetConsoleFlags(_universe, _verbosityLogFlags);
                #endif
            }
        }
    } else if (key == str::t_log_file) {
        if (value.IsHolding<std::string>()) {
            _logFile = value.UncheckedGet<std::string>();
            AiMsgSetLogFileName(_logFile.c_str());
        }
    } else if (key == str::t_enable_progressive_render) {
        if (!_isBatch) {
            _CheckForBoolValue(value, [&](const bool b) {
                AiRenderSetHintBool(GetRenderSession(), str::progressive, b);
                AiNodeSetBool(_options, str::enable_progressive_render, b);
            });
        }
    } else if (key == str::t_progressive_min_AA_samples) {
        if (!_isBatch) {
            _CheckForIntValue(value, [&](const int i) {
                AiRenderSetHintInt(GetRenderSession(), str::progressive_min_AA_samples, i);
            });
        }
    } else if (key == str::t_interactive_target_fps) {
        if (!_isBatch) {
            if (value.IsHolding<float>()) {
                AiRenderSetHintFlt(GetRenderSession(), str::interactive_target_fps, value.UncheckedGet<float>());
            }
        }
    } else if (key == str::t_interactive_target_fps_min) {
        if (!_isBatch) {
            if (value.IsHolding<float>()) {
                AiRenderSetHintFlt(GetRenderSession(), str::interactive_target_fps_min, value.UncheckedGet<float>());
            }
        }
    } else if (key == str::t_interactive_fps_min) {
        if (!_isBatch) {
            if (value.IsHolding<float>()) {
                AiRenderSetHintFlt(GetRenderSession(), str::interactive_fps_min, value.UncheckedGet<float>());
            }
        }
    } else if (key == str::t_profile_file) {
        if (value.IsHolding<std::string>()) {
            AiProfileSetFileName(value.UncheckedGet<std::string>().c_str());
        }
    } else if (key == _tokens->instantaneousShutter) {
        _CheckForBoolValue(value, [&](const bool b) { AiNodeSetBool(_options, str::ignore_motion_blur, b); });
    } else if (key == str::t_houdiniFps) {
        _CheckForFloatValue(value, [&](const float f) { _fps = f; });
    } else if (key == str::t_background) {
        ArnoldUsdCheckForSdfPathValue(value, [&](const SdfPath& p) { _background = p; });
    } else if (key == str::t_atmosphere) {
        ArnoldUsdCheckForSdfPathValue(value, [&](const SdfPath& p) { _atmosphere = p; });
    } else if (key == str::t_aov_shaders) {
        ArnoldUsdCheckForSdfPathVectorValue(value, [&](const SdfPathVector& p) { _aov_shaders = p; });
    } else if (key == str::t_imager) {
        ArnoldUsdCheckForSdfPathValue(value, [&](const SdfPath& p) { _imager = p; });
    } else if (key == str::t_subdiv_dicing_camera) {
        ArnoldUsdCheckForSdfPathValue(value, [&](const SdfPath& p) {
            _subdiv_dicing_camera = p; 
            AiNodeSetPtr(_options, str::subdiv_dicing_camera, LookupNode(_subdiv_dicing_camera.GetText()));
        });
    } else if (key == str::color_space_linear) {
        if (value.IsHolding<std::string>()) {
            AtNode* colorManager = getOrCreateColorManager(this, _options);
            AiNodeSetStr(colorManager, str::color_space_linear, AtString(value.UncheckedGet<std::string>().c_str()));
        }
    } else if (key == str::color_space_narrow) {
        if (value.IsHolding<std::string>()) {
            AtNode* colorManager = getOrCreateColorManager(this, _options);
            AiNodeSetStr(colorManager, str::color_space_narrow, AtString(value.UncheckedGet<std::string>().c_str()));
        }
    } else if (key == _tokens->dataWindowNDC) {
        if (value.IsHolding<GfVec4f>()) {
            _windowNDC = value.UncheckedGet<GfVec4f>();
        }
    } else if (key == _tokens->pixelAspectRatio) {
        if (value.IsHolding<float>()) {
            _pixelAspectRatio = value.UncheckedGet<float>();
        }
    } 
    else if (key == _tokens->resolution) {
        if (value.IsHolding<GfVec2i>()) {
            _resolution = value.UncheckedGet<GfVec2i>();
        }
    } else if (key == _tokens->batchCommandLine) {
        // Solaris-specific command line, it can have an argument "-o output.exr" to override
        // the output image. We might end up using this for arnold drivers
        if (value.IsHolding<VtStringArray>()) {
            const VtStringArray &commandLine = value.UncheckedGet<VtArray<std::string>>();
            for (unsigned int i = 0; i < commandLine.size(); ++i) {
                // husk argument for output image
                if (commandLine[i] == "-o" && i < commandLine.size() - 2) {
                    _outputOverride = commandLine[++i];
                    break;
                }
                // husk argument for thread count (#1077)
                if ((commandLine[i] == "-j" || commandLine[i] == "--threads") 
                        && i < commandLine.size() - 2) {
                    // if for some reason the argument value is not a number, atoi should return 0
                    // which is also the default arnold value. 
                    AiNodeSetInt(_options, str::threads, std::atoi(commandLine[++i].c_str()));
                }
            }
        }
    } 
    else {
        auto* optionsEntry = AiNodeGetNodeEntry(_options);
        // Sometimes the Render Delegate receives parameters that don't exist
        // on the options node. For example, if the host application ignores the
        // render setting descriptor list.
        if (AiNodeEntryLookUpParameter(optionsEntry, AtString(key.GetText())) != nullptr) {
            _SetNodeParam(_options, key, value);
        }
    }
}

void HdArnoldRenderDelegate::_ParseDelegateRenderProducts(const VtValue& value)
{
    // Details about the data layout can be found here:
    // https://www.sidefx.com/docs/hdk/_h_d_k__u_s_d_hydra.html#HDK_USDHydraHuskDRP
    // Delegate Render Products are used by husk, so we only have to parse them once.
    // We don't support cases where delegate render products are passed AFTER the first execution
    // of the render pass.
    if (!_delegateRenderProducts.empty()) {
        return;
    }
    using DataType = VtArray<HdAovSettingsMap>;
    if (!value.IsHolding<DataType>()) {
        return;
    }
    auto products = value.UncheckedGet<DataType>();
    // For Render Delegate products, we want to eventually create arnold drivers
    // during batch rendering #1422
    for (auto& productIter : products) {
        HdArnoldDelegateRenderProduct product;
        const auto* productType = TfMapLookupPtr(productIter, _tokens->productType);

        // check the product type, and see if we support it
        if (productType == nullptr || !productType->IsHolding<TfToken>())
            continue;

        TfToken renderProductType = productType->UncheckedGet<TfToken>();
        // We only consider render products with type set to "arnold",
        // as well as "deep" for backwards compatibility #1422
        if (renderProductType != str::t_arnold && renderProductType != _tokens->deep)
            continue;

        // default driver is exr
        TfToken driverType = _tokens->driver_exr;
        // Special case for "deep" for backwards compatibility, we want a deepexr driver
        if (renderProductType == _tokens->deep)
            driverType = str::t_driver_deepexr;
        else {
            const auto* arnoldDriver = TfMapLookupPtr(productIter, _tokens->arnoldDriver);
            if (arnoldDriver != nullptr ) { 
                // arnold:driver is set in this render product, we use that for the driver type
                if (arnoldDriver->IsHolding<TfToken>()) {
                    driverType = arnoldDriver->UncheckedGet<TfToken>();
                } else if (arnoldDriver->IsHolding<std::string>()) {
                    driverType = TfToken(arnoldDriver->UncheckedGet<std::string>());
                }
            }
        }


        // Let's check if a driver type exists as this render product type #1422
        if (AiNodeEntryLookUp(AtString(driverType.GetText())) == nullptr) {
            // Arnold doesn't know how to render with this driver, let's skip it
            AiMsgWarning("Unknown Arnold Driver Type %s", driverType.GetText());
            continue; 
        }

        // Ignoring cases where productName is not set.
        const auto* productName = TfMapLookupPtr(productIter, _tokens->productName);
        if (productName == nullptr || !productName->IsHolding<TfToken>()) {
            continue;
        }
        product.productName = productName->UncheckedGet<TfToken>();
        product.productType = driverType;
        productIter.erase(_tokens->productType);
        productIter.erase(_tokens->productName);
        // Elements of the HdAovSettingsMap in the product are either a list of RenderVars or generic attributes
        // of the render product.
        for (const auto& productElem : productIter) {
            // If the key is "aovDescriptor.aovSettings" then we got the list of RenderVars.
            if (productElem.first == _tokens->orderedVars) {
                if (!productElem.second.IsHolding<DataType>()) {
                    continue;
                }
                const auto& renderVars = productElem.second.UncheckedGet<DataType>();
                for (const auto& renderVarIter : renderVars) {
                    HdArnoldRenderVar renderVar;
                    renderVar.sourceType = _tokens->raw;
                    // Each element either contains a setting, or "aovDescriptor.aovSettings" which will hold
                    // extra settings for the RenderVar including metadata.
                    for (const auto& renderVarElem : renderVarIter) {
                        if (renderVarElem.first == _tokens->aovSettings) {
                            if (!renderVarElem.second.IsHolding<HdAovSettingsMap>()) {
                                continue;
                            }
                            renderVar.settings = renderVarElem.second.UncheckedGet<HdAovSettingsMap>();
                            // name is not coming through as a top parameter.
                            const auto* aovName = TfMapLookupPtr(renderVar.settings, _tokens->aovName);
                            if (aovName != nullptr) {
                                if (aovName->IsHolding<std::string>()) {
                                    renderVar.name = aovName->UncheckedGet<std::string>();
                                } else if (aovName->IsHolding<TfToken>()) {
                                    renderVar.name = aovName->UncheckedGet<TfToken>().GetString();
                                }
                            }
                        } else if (
                            renderVarElem.first == _tokens->sourceName &&
                            renderVarElem.second.IsHolding<std::string>()) {
                            renderVar.sourceName = renderVarElem.second.UncheckedGet<std::string>();
                        } else if (
                            renderVarElem.first == _tokens->sourceType && renderVarElem.second.IsHolding<TfToken>()) {
                            renderVar.sourceType = renderVarElem.second.UncheckedGet<TfToken>();
                        } else if (
                            renderVarElem.first == _tokens->dataType && renderVarElem.second.IsHolding<TfToken>()) {
                            renderVar.dataType = renderVarElem.second.UncheckedGet<TfToken>();
                        } else if (
                            renderVarElem.first == _tokens->format && renderVarElem.second.IsHolding<HdFormat>()) {
                            renderVar.format = renderVarElem.second.UncheckedGet<HdFormat>();
                        } else if (renderVarElem.first == _tokens->clearValue) {
                            renderVar.clearValue = renderVarElem.second;
                        } else if (
                            renderVarElem.first == _tokens->multiSampled && renderVarElem.second.IsHolding<bool>()) {
                            renderVar.multiSampled = renderVarElem.second.UncheckedGet<bool>();
                        }
                    }

                    // Look for driver:parameters:aov:format and arnold:format overrides
                    const auto* aovDriverFormat = TfMapLookupPtr(renderVar.settings, _tokens->aovDriverFormat);
                    if (aovDriverFormat != nullptr && aovDriverFormat->CanCast<TfToken>()) {
                        TfToken aovDriverFormatToken = VtValue::Cast<TfToken>(*aovDriverFormat).UncheckedGet<TfToken>();
                        renderVar.format = _GetHdFormatFromToken(aovDriverFormatToken);
                    }
                    const auto* arnoldFormat = TfMapLookupPtr(renderVar.settings, _tokens->aovFormat);
                    if (arnoldFormat != nullptr && arnoldFormat->CanCast<TfToken>()) {
                        TfToken arnoldFormatToken = VtValue::Cast<TfToken>(*arnoldFormat).UncheckedGet<TfToken>();
                        renderVar.format = _GetHdFormatFromToken(arnoldFormatToken);
                    }
                    // Any other cases should have good/reasonable defaults.
                    if (!renderVar.sourceName.empty() && !renderVar.name.empty()) {
                        product.renderVars.emplace_back(std::move(renderVar));
                    }
                }
            } else {
                // It's a setting describing the RenderProduct.
                product.settings.insert({productElem.first, productElem.second});
            }
        }
        _delegateRenderProducts.emplace_back(std::move(product));
    }
}

void HdArnoldRenderDelegate::SetRenderSetting(const TfToken& key, const VtValue& value)
{
    _renderParam->Interrupt();
    _SetRenderSetting(key, value);
}

VtValue HdArnoldRenderDelegate::GetRenderSetting(const TfToken& _key) const
{
    TfToken key;
    _RemoveArnoldGlobalPrefix(_key, key);
    if (key == str::t_enable_gpu_rendering) {
        return VtValue(AiNodeGetStr(_options, str::render_device) == str::GPU);
    } else if (key == str::t_enable_progressive_render) {
        bool v = true;
        AiRenderGetHintBool(GetRenderSession(), str::progressive, v);
        return VtValue(v);
    } else if (key == str::t_progressive_min_AA_samples) {
        int v = -4;
        AiRenderGetHintInt(GetRenderSession(), str::progressive_min_AA_samples, v);
        return VtValue(v);
    } else if (key == str::t_log_verbosity) {
        return VtValue(ArnoldUsdGetLogVerbosityFromFlags(_verbosityLogFlags));
    } else if (key == str::t_log_file) {
        return VtValue(_logFile);
    } else if (key == str::t_interactive_target_fps) {
        float v = 1.0f;
        AiRenderGetHintFlt(GetRenderSession(), str::interactive_target_fps, v);
        return VtValue(v);
    } else if (key == str::t_interactive_target_fps_min) {
        float v = 1.0f;
        AiRenderGetHintFlt(GetRenderSession(), str::interactive_target_fps_min, v);
        return VtValue(v);
    } else if (key == str::t_interactive_fps_min) {
        float v = 1.0f;
        AiRenderGetHintFlt(GetRenderSession(), str::interactive_fps_min, v);
        return VtValue(v);
    } else if (key == str::t_profile_file) {
        return VtValue(std::string(AiProfileGetFileName().c_str()));
    } else if (key == str::t_background) {
        return VtValue(_background.GetString());
    } else if (key == str::t_atmosphere) {
        return VtValue(_atmosphere.GetString());
    } else if (key == str::t_aov_shaders) {
        // There should be a function in common_utils.cpp
        std::vector<std::string> pathsAsString;
        std::transform(_aov_shaders.begin(), _aov_shaders.end(), std::back_inserter(pathsAsString), [](const auto &p){return p.GetString();});
        return VtValue(TfStringJoin(pathsAsString));
    } else if (key == str::t_imager) {
        return VtValue(_imager.GetString());
    }  else if (key == str::t_subdiv_dicing_camera) {
        return VtValue(_subdiv_dicing_camera.GetString());
    }
    const auto* nentry = AiNodeGetNodeEntry(_options);
    const auto* pentry = AiNodeEntryLookUpParameter(nentry, AtString(key.GetText()));
    return _GetNodeParamValue(_options, pentry);
}

// For now we only support a few parameter types, that are expected to have
// UI code in usdview / Maya to Hydra.
HdRenderSettingDescriptorList HdArnoldRenderDelegate::GetRenderSettingDescriptors() const
{
    const auto* nentry = AiNodeGetNodeEntry(_options);
    HdRenderSettingDescriptorList ret;
    for (const auto& it : _GetSupportedRenderSettings()) {
        HdRenderSettingDescriptor desc;
        desc.name = it.second.label;
        desc.key = it.first;
        if (it.second.defaultValue.IsEmpty()) {
            const auto* pentry = AiNodeEntryLookUpParameter(nentry, AtString(it.first.GetText()));
            desc.defaultValue = _GetNodeParamValue(_options, pentry);
        } else {
            desc.defaultValue = it.second.defaultValue;
        }
        ret.emplace_back(std::move(desc));
    }
    return ret;
}

VtDictionary HdArnoldRenderDelegate::GetRenderStats() const
{
    VtDictionary stats;

    float total_progress = 100.0f;
    AiRenderGetHintFlt(GetRenderSession(), str::total_progress, total_progress);
    stats[_tokens->percentDone] = total_progress;

    const double elapsed = _renderParam->GetElapsedRenderTime() / 1000.0;
    stats[_tokens->totalClockTime] = VtValue(elapsed);

    std::string renderStatus = _renderParam->GetRenderStatusString();
    if(!renderStatus.empty())
    {
        // Beautify the log - 'Rendering' looks nicer than 'rendering'
        // in the viewport annotation
        renderStatus[0] = std::toupper(renderStatus[0]);
    }
    const int width = AiNodeGetInt(_options, str::xres);
    const int height = AiNodeGetInt(_options, str::yres);
    constexpr std::size_t maxResChars{256};
    char resolutionBuffer[maxResChars];
    snprintf(&resolutionBuffer[0], maxResChars, "%s %i x %i", renderStatus.c_str(), width, height);
    stats[_tokens->renderProgressAnnotation] = VtValue(resolutionBuffer);

    // If there are cryptomatte drivers, we look for the metadata that is stored in each of them.
    // In theory, we could just look for the first driver, but for safety we're doing it for all of them
    for (const auto& cryptoDriver : _cryptomatteDrivers) {
        const AtNode *driver = LookupNode(cryptoDriver.c_str());
        if (!driver)
            continue;
        if (AiNodeLookUpUserParameter(driver, str::custom_attributes) == nullptr)
            continue;
        const AtArray *customAttrsArray = AiNodeGetArray(driver, str::custom_attributes);
        if (customAttrsArray == nullptr)
            continue;
        unsigned int customAttrsCount = AiArrayGetNumElements(customAttrsArray);
        for (unsigned int i = 0; i < customAttrsCount; ++i) {
            AtString customAttr = AiArrayGetStr(customAttrsArray, i);
            std::string customAttrStr(customAttr.c_str());
            // the custom_attributes attribute will be an array of strings, where each  
            // element is set like:
            // "STRING cryptomatte/f834d0a/conversion uint32_to_float32"
            // where the second element is the metadata name and the last one
            // is the metadata value
            size_t pos = customAttrStr.find_first_of(' ');
            if (pos == std::string::npos)
                continue;
            std::string customAttrType = customAttrStr.substr(0, pos);
            customAttrStr = customAttrStr.substr(pos + 1);

            pos = customAttrStr.find_first_of(' ');
            if (pos == std::string::npos)
                continue;
            std::string metadataName = customAttrStr.substr(0, pos);
            std::string metadataVal = customAttrStr.substr(pos + 1);
            // TODO do we want to check if the metadata is not a string ?
            stats[TfToken(metadataName)] = TfToken(metadataVal);
        }
    }
    return stats;
}

HdResourceRegistrySharedPtr HdArnoldRenderDelegate::GetResourceRegistry() const { return _resourceRegistry; }

HdRenderPassSharedPtr HdArnoldRenderDelegate::CreateRenderPass(
    HdRenderIndex* index, const HdRprimCollection& collection)
{
    return HdRenderPassSharedPtr(new HdArnoldRenderPass(this, index, collection));
}

#if PXR_VERSION >= 2102
HdInstancer* HdArnoldRenderDelegate::CreateInstancer(HdSceneDelegate* delegate, const SdfPath& id)
{
    return new HdArnoldInstancer(this, delegate, id);
#else
HdInstancer* HdArnoldRenderDelegate::CreateInstancer(
    HdSceneDelegate* delegate, const SdfPath& id, const SdfPath& instancerId)
{
    return new HdArnoldInstancer(this, delegate, id, instancerId);
#endif
}

void HdArnoldRenderDelegate::DestroyInstancer(HdInstancer* instancer) { delete instancer; }

#if PXR_VERSION >= 2102
HdRprim* HdArnoldRenderDelegate::CreateRprim(const TfToken& typeId, const SdfPath& rprimId)
{
    if (!(_mask & AI_NODE_SHAPE))
        return nullptr;

    _renderParam->Interrupt();
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdArnoldMesh(this, rprimId);
    }
    if (typeId == HdPrimTypeTokens->volume) {
        return new HdArnoldVolume(this, rprimId);
    }
    if (typeId == HdPrimTypeTokens->points) {
        return new HdArnoldPoints(this, rprimId);
    }
    if (typeId == HdPrimTypeTokens->basisCurves) {
        return new HdArnoldBasisCurves(this, rprimId);
    }
    if (typeId == str::t_procedural_custom) {
        return new HdArnoldProceduralCustom(this, rprimId);
    }
    auto typeIt = _nativeRprimTypes.find(typeId);
    if (typeIt != _nativeRprimTypes.end()) {
        return new HdArnoldNativeRprim(this, typeIt->second, rprimId);
    }
    TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
    return nullptr;
}
#else
HdRprim* HdArnoldRenderDelegate::CreateRprim(const TfToken& typeId, const SdfPath& rprimId, const SdfPath& instancerId)
{
    if (!(_mask & AI_NODE_SHAPE))
        return nullptr;

    _renderParam->Interrupt();
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdArnoldMesh(this, rprimId, instancerId);
    }
    if (typeId == HdPrimTypeTokens->volume) {
        return new HdArnoldVolume(this, rprimId, instancerId);
    }
    if (typeId == HdPrimTypeTokens->points) {
        return new HdArnoldPoints(this, rprimId, instancerId);
    }
    if (typeId == HdPrimTypeTokens->basisCurves) {
        return new HdArnoldBasisCurves(this, rprimId, instancerId);
    }
    auto typeIt = _nativeRprimTypes.find(typeId);
    if (typeIt != _nativeRprimTypes.end()) {
        return new HdArnoldNativeRprim(this, typeIt->second, rprimId, instancerId);
    }
    TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
    return nullptr;
}
#endif

void HdArnoldRenderDelegate::DestroyRprim(HdRprim* rPrim)
{
    _renderParam->Interrupt();
    delete rPrim;
}

HdSprim* HdArnoldRenderDelegate::CreateSprim(const TfToken& typeId, const SdfPath& sprimId)
{
    _renderParam->Interrupt();
    // We're creating a new Sprim. It's possible that it is already referenced
    // by another prim (which can happen when shaders are disconnected/reconnected).
    // In this case we need to dirty it so that all source prims are properly updated.
    // Note : for now we're only tracking dependencies for Sprim targets, but 
    // this could be extended
    const auto &it = _targetToSourcesMap.find(sprimId);
    if (it != _targetToSourcesMap.end())
        DirtyDependency(sprimId);

    if (typeId == HdPrimTypeTokens->camera) {
        return (_mask & AI_NODE_CAMERA) ? 
            new HdArnoldCamera(this, sprimId) : nullptr;
    }
    if (typeId == HdPrimTypeTokens->material) {
        return (_mask & AI_NODE_SHADER) ? 
            new HdArnoldNodeGraph(this, sprimId) : nullptr;
    }
    if (typeId == _tokens->ArnoldOptions) {
        return (_mask & AI_NODE_OPTIONS) ? 
            new HdArnoldOptions(this, sprimId) : nullptr;
    }

    if (typeId == HdPrimTypeTokens->sphereLight) {
        return (_mask & AI_NODE_LIGHT) ? 
            HdArnoldLight::CreatePointLight(this, sprimId) : nullptr;
    }
    if (typeId == HdPrimTypeTokens->distantLight) {
        return (_mask & AI_NODE_LIGHT) ? 
            HdArnoldLight::CreateDistantLight(this, sprimId) : nullptr;
    }
    if (typeId == HdPrimTypeTokens->diskLight) {
        return (_mask & AI_NODE_LIGHT) ? 
            HdArnoldLight::CreateDiskLight(this, sprimId) : nullptr;
    }
    if (typeId == HdPrimTypeTokens->rectLight) {
        return (_mask & AI_NODE_LIGHT) ? 
            HdArnoldLight::CreateRectLight(this, sprimId) : nullptr;
    }
    if (typeId == HdPrimTypeTokens->cylinderLight) {
        return (_mask & AI_NODE_LIGHT) ? 
            HdArnoldLight::CreateCylinderLight(this, sprimId) : nullptr;
    }
    if (typeId == HdPrimTypeTokens->domeLight) {
        return (_mask & AI_NODE_LIGHT) ? 
            HdArnoldLight::CreateDomeLight(this, sprimId) : nullptr;
    }
    if (typeId == _tokens->GeometryLight) {
        return (_mask & AI_NODE_LIGHT) ? 
            HdArnoldLight::CreateGeometryLight(this, sprimId) : nullptr;
    }
    if (typeId == HdPrimTypeTokens->simpleLight) {
        return nullptr;
    }
    if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(sprimId);
    }
    TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    return nullptr;
}

HdSprim* HdArnoldRenderDelegate::CreateFallbackSprim(const TfToken& typeId)
{
    // TODO Do we need fallback sprims ? in which case ??
    // _renderParam->Interrupt();
    // if (typeId == HdPrimTypeTokens->camera) {
    //     return new HdArnoldCamera(this, SdfPath::EmptyPath());
    // }
    // if (typeId == HdPrimTypeTokens->material) {
    //     return new HdArnoldNodeGraph(this, SdfPath::EmptyPath());
    // }
    // if (typeId == HdPrimTypeTokens->sphereLight) {
    //     return HdArnoldLight::CreatePointLight(this, SdfPath::EmptyPath());
    // }
    // if (typeId == HdPrimTypeTokens->distantLight) {
    //     return HdArnoldLight::CreateDistantLight(this, SdfPath::EmptyPath());
    // }
    // if (typeId == HdPrimTypeTokens->diskLight) {
    //     return HdArnoldLight::CreateDiskLight(this, SdfPath::EmptyPath());
    // }
    // if (typeId == HdPrimTypeTokens->rectLight) {
    //     return HdArnoldLight::CreateRectLight(this, SdfPath::EmptyPath());
    // }
    // if (typeId == HdPrimTypeTokens->cylinderLight) {
    //     return HdArnoldLight::CreateCylinderLight(this, SdfPath::EmptyPath());
    // }
    // if (typeId == HdPrimTypeTokens->domeLight) {
    //     return HdArnoldLight::CreateDomeLight(this, SdfPath::EmptyPath());
    // }
    // if (typeId == _tokens->GeometryLight) {
    //     return nullptr;
    // }
    // if (typeId == HdPrimTypeTokens->simpleLight) {
    //     return nullptr;
    // }
    // if (typeId == HdPrimTypeTokens->extComputation) {
    //     return new HdExtComputation(SdfPath::EmptyPath());
    // }
    // TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    return nullptr;
}

void HdArnoldRenderDelegate::DestroySprim(HdSprim* sPrim)
{
    if (sPrim == nullptr)
        return;

    _renderParam->Interrupt();
    const auto &id = sPrim->GetId();
    // We could be destroying a Sprim that is being referenced by 
    // another source. We need to keep track of this, so that
    // all the references are properly updated
    if (_targetToSourcesMap.find(id) != _targetToSourcesMap.end())
        RemoveDependency(id);
    delete sPrim;
}

HdBprim* HdArnoldRenderDelegate::CreateBprim(const TfToken& typeId, const SdfPath& bprimId)
{
    // Neither of these will create Arnold nodes.
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdArnoldRenderBuffer(bprimId);
    }
    if (typeId == _tokens->openvdbAsset) {
        return new HdArnoldOpenvdbAsset(this, bprimId);
    }
    TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    return nullptr;
}

HdBprim* HdArnoldRenderDelegate::CreateFallbackBprim(const TfToken& typeId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdArnoldRenderBuffer(SdfPath());
    }
    if (typeId == _tokens->openvdbAsset) {
        return new HdArnoldOpenvdbAsset(this, SdfPath());
    }
    TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    return nullptr;
}

void HdArnoldRenderDelegate::DestroyBprim(HdBprim* bPrim)
{
    // RenderBuffers can be in use in drivers.
    _renderParam->Interrupt();
    delete bPrim;
}

TfToken HdArnoldRenderDelegate::GetMaterialBindingPurpose() const { return HdTokens->full; }
#if PXR_VERSION >= 2105

TfTokenVector HdArnoldRenderDelegate::GetMaterialRenderContexts() const
{
    return {_tokens->arnold, str::t_mtlx};
}

#else

TfToken HdArnoldRenderDelegate::GetMaterialNetworkSelector() const { return _tokens->arnold; }

#endif

AtString HdArnoldRenderDelegate::GetLocalNodeName(const AtString& name) const
{
    return AtString(_id.AppendChild(TfToken(name.c_str())).GetText());
}

AtUniverse* HdArnoldRenderDelegate::GetUniverse() const { return _universe; }

AtRenderSession* HdArnoldRenderDelegate::GetRenderSession() const
{
    if (_renderDelegateOwnsUniverse) {
        return _renderSession;
    } else {
        return AiUniverseGetRenderSession(GetUniverse());
    }
}

AtNode* HdArnoldRenderDelegate::GetOptions() const { return _options; }

AtNode* HdArnoldRenderDelegate::GetFallbackSurfaceShader() const { return _fallbackShader; }

AtNode* HdArnoldRenderDelegate::GetFallbackVolumeShader() const { return _fallbackVolumeShader; }

HdAovDescriptor HdArnoldRenderDelegate::GetDefaultAovDescriptor(const TfToken& name) const
{
    if (name == HdAovTokens->color) {
#if 1
        return HdAovDescriptor(HdFormatFloat32Vec4, false, VtValue(GfVec4f(0.0f)));
#else
        return HdAovDescriptor(HdFormatUNorm8Vec4, false, VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 0.0f)));
#endif
    } else if (name == HdAovTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
    } else if (name == HdAovTokens->primId) {
        return HdAovDescriptor(HdFormatInt32, false, VtValue(-1));
    } else if (name == HdAovTokens->instanceId || name == HdAovTokens->elementId || name == HdAovTokens->pointId) {
        // We are only supporting the prim id buffer for now.
        return HdAovDescriptor(HdFormatInt32, false, VtValue(-1));
    } else if (
        name == HdAovTokens->normal || name == HdAovTokens->Neye ||
        name == "linearDepth" || // This was changed to cameraDepth after 0.19.11.
        name == "cameraDepth") {
        // More built-in aovs.
        return HdAovDescriptor();
    } else if (TfStringStartsWith(name.GetString(), HdAovTokens->primvars)) {
        // Primvars.
        return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    } else if (TfStringStartsWith(name.GetString(), HdAovTokens->lpe)) {
        // LPEs.
        return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    } else {
        // Anything else. We can't decide what the AOV might contain based on the name, so we are just returning a
        // default value.
        return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    }
}

void HdArnoldRenderDelegate::RegisterLightLinking(const TfToken& name, HdLight* light, bool isShadow)
{
    std::lock_guard<std::mutex> guard(_lightLinkingMutex);
    auto& links = isShadow ? _shadowLinks : _lightLinks;
    auto it = links.find(name);
    if (it == links.end()) {
        if (!name.IsEmpty() || !links.empty()) {
            _lightLinkingChanged.store(true, std::memory_order_release);
        }
        links.emplace(name, std::vector<HdLight*>{light});
    } else {
        if (std::find(it->second.begin(), it->second.end(), light) == it->second.end()) {
            // We only trigger the change if we are registering a non-empty collection, or there are more than one
            // collections.
            if (!name.IsEmpty() || links.size() > 1) {
                _lightLinkingChanged.store(true, std::memory_order_release);
            }
            it->second.push_back(light);
        }
    }
}

void HdArnoldRenderDelegate::DeregisterLightLinking(const TfToken& name, HdLight* light, bool isShadow)
{
    std::lock_guard<std::mutex> guard(_lightLinkingMutex);
    auto& links = isShadow ? _shadowLinks : _lightLinks;
    auto it = links.find(name);
    if (it != links.end()) {
        // We only trigger updates if either deregistering a named collection, or deregistering the empty
        // collection and there are other collection.
        if (!name.IsEmpty() || links.size() > 1) {
            _lightLinkingChanged.store(true, std::memory_order_release);
        }
        it->second.erase(std::remove(it->second.begin(), it->second.end(), light), it->second.end());
        if (it->second.empty()) {
            links.erase(name);
        }
    }
}

void HdArnoldRenderDelegate::_ApplyLightLinking(AtNode* shape, const VtArray<TfToken>& categories)
{
    std::lock_guard<std::mutex> guard(_lightLinkingMutex);
    // We need to reset the parameter if either there are no light links, or the only light link is the default
    // group.
    const auto lightEmpty = _lightLinks.empty() || (_lightLinks.size() == 1 && _lightLinks.count(TfToken{}) == 1);
    const auto shadowEmpty = _shadowLinks.empty() || (_shadowLinks.size() == 1 && _shadowLinks.count(TfToken{}) == 1);
    if (lightEmpty) {
        AiNodeResetParameter(shape, str::use_light_group);
        AiNodeResetParameter(shape, str::light_group);
    }
    if (shadowEmpty) {
        AiNodeResetParameter(shape, str::use_shadow_group);
        AiNodeResetParameter(shape, str::shadow_group);
    }
    if (lightEmpty && shadowEmpty) {
        return;
    }
    auto applyGroups = [&](const AtString& group, const AtString& useGroup, const LightLinkingMap& links) {
        std::vector<AtNode*> lights;
        for (const auto& category : categories) {
            auto it = links.find(category);
            if (it != links.end()) {
                for (auto* light : it->second) {
                    auto* arnoldLight = HdArnoldLight::GetLightNode(light);
                    if (arnoldLight != nullptr) {
                        lights.push_back(arnoldLight);
                    }
                }
            }
        }
        // Add the lights with an empty collection to the list.
        auto it = links.find(TfToken{});
        if (it != links.end()) {
            for (auto* light : it->second) {
                auto* arnoldLight = HdArnoldLight::GetLightNode(light);
                if (arnoldLight != nullptr) {
                    lights.push_back(arnoldLight);
                }
            }
        }

        // Add the mesh lights as well, they are not registered as light in hydra unfortunatelly
        {
            std::lock_guard<std::mutex> guard(_meshLightsMutex);
            for (AtNode * meshLight:_meshLights) {
                lights.push_back(meshLight); // TODO except if they have a collection yeah
            }
        }

        // If lights is empty, then no lights affect the shape, and we still have to set useGroup to true.
        if (lights.empty()) {
            AiNodeResetParameter(shape, group);
        } else {
            AiNodeSetArray(
                shape, group, AiArrayConvert(static_cast<uint32_t>(lights.size()), 1, AI_TYPE_NODE, lights.data()));
        }
        AiNodeSetBool(shape, useGroup, true);
    };
    if (!lightEmpty) {
        applyGroups(str::light_group, str::use_light_group, _lightLinks);
    }
    if (!shadowEmpty) {
        applyGroups(str::shadow_group, str::use_shadow_group, _shadowLinks);
    }
}

void HdArnoldRenderDelegate::ApplyLightLinking(HdSceneDelegate *sceneDelegate, AtNode* node, SdfPath const& id) {
    const SdfPath &instancerId = sceneDelegate->GetInstancerId(id);
    VtArray<TfToken> instancerCategories;
    // If this shape is instanced, we store the list of "categories"
    // (aka collections) associated with it.
    if (!instancerId.IsEmpty()) {
        instancerCategories = sceneDelegate->GetCategories(instancerId);
    }
    if (instancerCategories.empty()) {
        // If there are no collections associated with eventual instancers,
        // we just pass the reference to the categories array to avoid useless copies
        _ApplyLightLinking(node, sceneDelegate->GetCategories(id));
    } else {
        // We want to concatenate the shape's categories with the
        // instancer's categories, and call ApplyLightLinking with the full list
        VtArray<TfToken> categories = sceneDelegate->GetCategories(id);
        categories.reserve(categories.size() + instancerCategories.size());
        for (const auto &instanceCategory : instancerCategories)
            categories.push_back(instanceCategory);
        _ApplyLightLinking(node, categories);
    }
}


void HdArnoldRenderDelegate::ProcessConnections()
{
    _apiAdapter.ProcessConnections();
}
bool HdArnoldRenderDelegate::ShouldSkipIteration(HdRenderIndex* renderIndex, const GfVec2f& shutter)
{
    HdDirtyBits bits = HdChangeTracker::Clean;
    // If Light Linking have changed, we have to dirty the categories on all rprims to force updating the
    // the light linking information.
    if (_lightLinkingChanged.exchange(false, std::memory_order_acq_rel)) {
        bits |= HdChangeTracker::DirtyCategories;
    }

    // MeshLight changes
    if (_meshLightsChanged.exchange(false, std::memory_order_acq_rel)) {
        bits |= HdChangeTracker::DirtyCategories;
    }

    // When shutter open and shutter close significantly changes, we might not have enough samples for transformation
    // and deformation, so we need to force re-syncing all the prims.
    if (_renderParam->UpdateShutter(shutter)) {
        bits |= HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyInstancer |
                HdChangeTracker::DirtyPrimvar;
    }
    /// TODO(pal): Investigate if this is needed.
    /// When FPS changes we have to dirty points and primvars.
    if (_renderParam->UpdateFPS(_fps)) {
        bits |= HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyPrimvar;
    }
    auto& changeTracker = renderIndex->GetChangeTracker();
    
    auto skip = false;
    if (bits != HdChangeTracker::Clean) {
        renderIndex->GetChangeTracker().MarkAllRprimsDirty(bits);
        skip = true;
    }
    SdfPath id;
    auto markPrimDirty = [&](const SdfPath& source, HdDirtyBits bits) {
        // Marking a primitive as being dirty. But the function to invoke
        // depends on the prim type. For now we're checking first if a Rprim
        // exists with this name, to choose between Rprims and Sprims.
        if (renderIndex->HasRprim(source)) {
            changeTracker.MarkRprimDirty(source, bits);
        }
        else {
            // Depending on the Sprim type, the dirty bits must be different. See .//pxr/imaging/hd/dirtyBitsTranslator.cpp
            changeTracker.MarkSprimDirty(source, bits);
        }
    };
    // First let's process all the dependencies that were removed.
    // We need to remove it from all our maps, and mark all the 
    // sources as being dirty, so that they can update their 
    // new reference properly
    while (_dependencyRemovalQueue.try_pop(id)) {        
        auto targetIt = _targetToSourcesMap.find(id);
        if (targetIt != _targetToSourcesMap.end()) {
            skip = true; // this requires a render update
            for (const auto& source : targetIt->second) {
                // for each source referencing the current target
                // we need to remove the target from its list
                auto sourceIt = _sourceToTargetsMap.find(source);
                if (sourceIt != _sourceToTargetsMap.end()) {
                    sourceIt->second.erase(id);
                }
                if (sourceIt->second.empty()) {
                    _sourceToTargetsMap.erase(sourceIt);
                }
                // This source primitive needs to be updated
                auto bits = _dependencyToDirtyBitsMap[{id, source}];
                markPrimDirty(source, bits);
                _dependencyToDirtyBitsMap.erase({id, source});
            }

            // Erase the map from this target to all its sources
            _targetToSourcesMap.erase(id);
        }        
    }

    ArnoldDependencyChange dependencyChange;
    while (_dependencyTrackQueue.try_pop(dependencyChange)) {
        // We have a new list of dependencies for a given source.
        // We need to ensure that the previous dependencies were properly cleared
        const auto &newTargetsWithBits = dependencyChange.targets;
        const auto &source = dependencyChange.source;
        auto prevTargets = _sourceToTargetsMap[source];
        PathSet newTargets;
        for (const auto &pathAndBits : newTargetsWithBits) {
            newTargets.insert(pathAndBits.first);
            _dependencyToDirtyBitsMap.insert({{pathAndBits.first, source}, pathAndBits.second});
        }
        // Set the new targets for this source
        _sourceToTargetsMap[source] = newTargets;

        // Now check, for all targets that were set previously to this source,
        // if they're still present in the new list. If they're not, then we need
        // to remove the source from the target map
        for (const auto &prevTarget : prevTargets) {
            if (newTargets.find(prevTarget) == newTargets.end()) {
                _targetToSourcesMap[prevTarget].erase(source);
                _dependencyToDirtyBitsMap.erase({prevTarget, source});
            }
        }
        
        for (const auto& target : newTargets) {
            // for each target, we want to add all the source to its map
            _targetToSourcesMap[target].insert(source);
        }
    }
    
    // Finally, we're processing all the dependencies that were marked as dirty.
    // For each of them, we need to update all the sources pointing at it
    while (_dependencyDirtyQueue.try_pop(id)) {
        auto it = _targetToSourcesMap.find(id);
        if (it != _targetToSourcesMap.end()) {
            skip = true;
            // mark each source as being dirty
            for (const auto &source: it->second) {
                auto bits = _dependencyToDirtyBitsMap[{id, source}];
                markPrimDirty(source, bits);
            }
        }
    }
    if (!skip)
        ProcessConnections();
    return skip;
}

bool HdArnoldRenderDelegate::IsPauseSupported() const { return true; }

bool HdArnoldRenderDelegate::Pause()
{
    _renderParam->Pause();
    return true;
}

bool HdArnoldRenderDelegate::Resume()
{
    _renderParam->Resume();
    return true;
}

const HdArnoldRenderDelegate::NativeRprimParamList* HdArnoldRenderDelegate::GetNativeRprimParamList(
    const AtString& arnoldNodeType) const
{
    const auto it = _nativeRprimParams.find(arnoldNodeType);
    return it == _nativeRprimParams.end() ? nullptr : &it->second;
}

void HdArnoldRenderDelegate::DirtyDependency(const SdfPath& id) 
{
    _dependencyDirtyQueue.emplace(id);
}

void HdArnoldRenderDelegate::RemoveDependency(const SdfPath& id) 
{
    _dependencyRemovalQueue.emplace(id); 
}

void HdArnoldRenderDelegate::TrackDependencies(const SdfPath& source, const PathSetWithDirtyBits& targets)
{
    _dependencyTrackQueue.emplace(source, targets);
}

void HdArnoldRenderDelegate::ClearDependencies(const SdfPath& source)
{
    // Originaly TrackDependencies(source, {});
    auto it = _sourceToTargetsMap.find(source);
    if (it != _sourceToTargetsMap.end()) {
        for(const auto &target: it->second) {
            _dependencyRemovalQueue.emplace(target);
        }
    }
}

void HdArnoldRenderDelegate::TrackRenderTag(AtNode* node, const TfToken& tag)
{
    if (!IsBatchContext()) {
        AiNodeSetDisabled(node, std::find(_renderTags.begin(), _renderTags.end(), tag) == _renderTags.end());
        _renderTagTrackQueue.push({node, tag});
    }
}

void HdArnoldRenderDelegate::UntrackRenderTag(AtNode* node) { _renderTagUntrackQueue.push(node); }

void HdArnoldRenderDelegate::SetRenderTags(const TfTokenVector& renderTags)
{
    RenderTagTrackQueueElem renderTagRegister;
    while (_renderTagTrackQueue.try_pop(renderTagRegister)) {
        _renderTagMap[renderTagRegister.first] = renderTagRegister.second;
    }
    AtNode* node;
    while (_renderTagUntrackQueue.try_pop(node)) {
        _renderTagMap.erase(node);
    }
    if (renderTags != _renderTags) {
        _renderTags = renderTags;
        for (auto& elem : _renderTagMap) {
            const auto disabled = std::find(_renderTags.begin(), _renderTags.end(), elem.second) == _renderTags.end();
            AiNodeSetDisabled(elem.first, disabled);
        }
        _renderParam->Interrupt();
    }
}

AtNode* HdArnoldRenderDelegate::GetBackground(HdRenderIndex* renderIndex)
{
    const HdArnoldNodeGraph *nodeGraph = HdArnoldNodeGraph::GetNodeGraph(renderIndex, _background);
    if (nodeGraph)    
        return nodeGraph->GetTerminal(str::t_background);
    return nullptr;
}

AtNode* HdArnoldRenderDelegate::GetAtmosphere(HdRenderIndex* renderIndex)
{
    const HdArnoldNodeGraph *nodeGraph = HdArnoldNodeGraph::GetNodeGraph(renderIndex, _atmosphere);
    if (nodeGraph)
        return nodeGraph->GetTerminal(str::t_atmosphere);
    return nullptr;
}

std::vector<AtNode*> HdArnoldRenderDelegate::GetAovShaders(HdRenderIndex* renderIndex)
{
    std::vector<AtNode *> nodes;
    for (const auto &materialPath: _aov_shaders) {
        const HdArnoldNodeGraph *nodeGraph = HdArnoldNodeGraph::GetNodeGraph(renderIndex, materialPath);
        if (nodeGraph) {
            const auto &terminals = nodeGraph->GetTerminals(_tokens->aovShadersArray);
            std::copy(terminals.begin(), terminals.end(), std::back_inserter(nodes));
        }
    }
    return nodes;
}

AtNode* HdArnoldRenderDelegate::GetImager(HdRenderIndex* renderIndex)
{
    const HdArnoldNodeGraph *nodeGraph = HdArnoldNodeGraph::GetNodeGraph(renderIndex, _imager);
    if (nodeGraph)
        return nodeGraph->GetTerminal(str::t_input);
    return nullptr;
}

AtNode* HdArnoldRenderDelegate::GetSubdivDicingCamera(HdRenderIndex* renderIndex)
{
    if (_subdiv_dicing_camera.IsEmpty())
        return nullptr;

    return LookupNode(_subdiv_dicing_camera.GetText());
}

void HdArnoldRenderDelegate::RegisterCryptomatteDriver(const AtString& driver)
{
    _cryptomatteDrivers.insert(driver);

}
void HdArnoldRenderDelegate::ClearCryptomatteDrivers()
{
   _cryptomatteDrivers.clear();
}

#if PXR_VERSION >= 2108
HdCommandDescriptors HdArnoldRenderDelegate::GetCommandDescriptors() const
{
    HdCommandDescriptors descriptors;
    descriptors.emplace_back(TfToken("flush_texture"), "Flush textures");
    return descriptors;
}

bool HdArnoldRenderDelegate::InvokeCommand(const TfToken& command, const HdCommandArgs& args)
{
    if (command == TfToken("flush_texture")) {
        // Pause render
        _renderParam->Pause();
        // Flush texture
        AiUniverseCacheFlush(_universe, AI_CACHE_TEXTURE);
        // Restart the render
        _renderParam->Resume();
    }
    return false;
}
#endif // PXR_VERSION

PXR_NAMESPACE_CLOSE_SCOPE
