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
#include "material.h"
#include "mesh.h"
#include "native_rprim.h"
#include "nodes/nodes.h"
#include "openvdb_asset.h"
#include "points.h"
#include "render_buffer.h"
#include "render_pass.h"
#include "volume.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (arnold)
    (openvdbAsset)
    ((arnoldGlobal, "arnold:global:"))
    (percentDone)
    (delegateRenderProducts)
    (orderedVars)
    ((aovSettings, "aovDescriptor.aovSettings"))
    (productType)
    (productName)
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
);
// clang-format on

#define PXR_VERSION_STR ARNOLD_XSTR(PXR_MAJOR_VERSION) "." ARNOLD_XSTR(PXR_MINOR_VERSION) "." ARNOLD_XSTR(PXR_PATCH_VERSION)

namespace {

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
        {str::t_GI_transmission_depth, {"Transmission Depth"}},
        {str::t_GI_volume_depth, {"Volume Depth"}},
        {str::t_GI_total_depth, {"Total Depth"}},
        // Ignore settings
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

int _GetLogVerbosityFromFlags(int flags)
{
    // This isn't an exact mapping, as verbosity can't emcompass all possible
    // flag combinations... so we just check for certain flags, and assume
    if (flags == 0) {
        return 0;
    };
    if (flags & AI_LOG_DEBUG) {
        return 5;
    }
    if (flags & (AI_LOG_STATS | AI_LOG_PLUGINS)) {
        return 4;
    }
    if (flags & (AI_LOG_INFO | AI_LOG_PROGRESS)) {
        return 3;
    }
    if (flags & (AI_LOG_WARNINGS)) {
        return 2;
    }
    return 1;
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
    key_new =
        TfStringStartsWith(key, _tokens->arnoldGlobal) ? TfToken{key.GetText() + _tokens->arnoldGlobal.size()} : key;
}

} // namespace

std::mutex HdArnoldRenderDelegate::_mutexResourceRegistry;
std::atomic_int HdArnoldRenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdArnoldRenderDelegate::_resourceRegistry;

HdArnoldRenderDelegate::HdArnoldRenderDelegate(HdArnoldRenderContext context) : _context(context)
{
    _lightLinkingChanged.store(false, std::memory_order_release);
    _id = SdfPath(TfToken(TfStringPrintf("/HdArnoldRenderDelegate_%p", this)));

    // We first need to check if arnold has already been initialized.
    // If not, we need to call AiBegin, and the destructor on we'll call AiEnd
    _isArnoldActive = AiUniverseIsActive();
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
                            HdPrimTypeTokens->basisCurves};
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
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
    if (_counterResourceRegistry.fetch_add(1) == 0) {
        _resourceRegistry.reset(new HdResourceRegistry());
    }

    const auto& config = HdArnoldConfig::GetInstance();
    if (config.log_flags_console >= 0) {
        _ignoreVerbosityLogFlags = true;
        AiMsgSetConsoleFlags(config.log_flags_console);
    } else {
        AiMsgSetConsoleFlags(_verbosityLogFlags);
    }
    if (config.log_flags_file >= 0) {
        AiMsgSetLogFileFlags(config.log_flags_file);
    }
    hdArnoldInstallNodes();

#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    _universe = AiUniverse();
    _renderSession = AiRenderSession(_universe, AI_SESSION_INTERACTIVE);
#else
    _universe = nullptr;
#endif
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    _renderParam.reset(new HdArnoldRenderParam(this));
#else
    _renderParam.reset(new HdArnoldRenderParam());
#endif
    // To set the default value.
    _fps = _renderParam->GetFPS();
    _options = AiUniverseGetOptions(_universe);
    for (const auto& o : _GetSupportedRenderSettings()) {
        _SetRenderSetting(o.first, o.second.defaultValue);
    }

#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    AiRenderSetHintStr(
        _renderSession, str::render_context, _context == HdArnoldRenderContext::Hydra ? str::hydra : str::husk);
#else
    AiRenderSetHintStr(str::render_context, _context == HdArnoldRenderContext::Hydra ? str::hydra : str::husk);
#endif
    _fallbackShader = AiNode(_universe, str::standard_surface);
    AiNodeSetStr(_fallbackShader, str::name, AtString(TfStringPrintf("fallbackShader_%p", _fallbackShader).c_str()));
    auto* userDataReader = AiNode(_universe, str::user_data_rgb);
    AiNodeSetStr(
        userDataReader, str::name,
        AtString(TfStringPrintf("fallbackShader_userDataReader_%p", userDataReader).c_str()));
    AiNodeSetStr(userDataReader, str::attribute, str::displayColor);
    AiNodeSetRGB(userDataReader, str::_default, 1.0f, 1.0f, 1.0f);
    AiNodeLink(userDataReader, str::base_color, _fallbackShader);

    _fallbackVolumeShader = AiNode(_universe, str::standard_volume);
    AiNodeSetStr(
        _fallbackVolumeShader, str::name, AtString(TfStringPrintf("fallbackVolume_%p", _fallbackVolumeShader).c_str()));

    // We need access to both beauty and P at the same time.
    if (_context == HdArnoldRenderContext::Husk) {
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        AiRenderSetHintBool(_renderSession, str::progressive, false);
#else
        AiRenderSetHintBool(str::progressive, false);
#endif
        AiNodeSetBool(_options, str::enable_progressive_render, false);
    } else {
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        AiRenderSetHintBool(_renderSession, str::progressive_show_all_outputs, true);
#else
        AiRenderSetHintBool(str::progressive_show_all_outputs, true);
#endif
    }
}

HdArnoldRenderDelegate::~HdArnoldRenderDelegate()
{
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
    if (_counterResourceRegistry.fetch_sub(1) == 1) {
        _resourceRegistry.reset();
    }
    _renderParam->Interrupt();
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    AiRenderSessionDestroy(_renderSession);
#endif
    hdArnoldUninstallNodes();
    AiUniverseDestroy(_universe);
    // We must end the arnold session, only if we created it during the constructor.
    // Otherwise we could be destroying a session that is being used elsewhere
    if (!_isArnoldActive)
        AiEnd();
}

HdRenderParam* HdArnoldRenderDelegate::GetRenderParam() const { return _renderParam.get(); }

void HdArnoldRenderDelegate::CommitResources(HdChangeTracker* tracker) {}

const TfTokenVector& HdArnoldRenderDelegate::GetSupportedRprimTypes() const { return _supportedRprimTypes; }

const TfTokenVector& HdArnoldRenderDelegate::GetSupportedSprimTypes() const { return _SupportedSprimTypes(); }

const TfTokenVector& HdArnoldRenderDelegate::GetSupportedBprimTypes() const { return _SupportedBprimTypes(); }

void HdArnoldRenderDelegate::_SetRenderSetting(const TfToken& _key, const VtValue& _value)
{
    // Special setting that describes custom output, like deep AOVs.
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
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
            AiDeviceAutoSelect(_renderSession);
#else
            AiDeviceAutoSelect();
#endif
        });
    } else if (key == str::t_log_verbosity) {
        if (value.IsHolding<int>()) {
            _verbosityLogFlags = _GetLogFlagsFromVerbosity(value.UncheckedGet<int>());
            if (!_ignoreVerbosityLogFlags) {
                AiMsgSetConsoleFlags(_verbosityLogFlags);
            }
        }
    } else if (key == str::t_log_file) {
        if (value.IsHolding<std::string>()) {
            _logFile = value.UncheckedGet<std::string>();
            AiMsgSetLogFileName(_logFile.c_str());
        }
    } else if (key == str::t_enable_progressive_render) {
        if (_context != HdArnoldRenderContext::Husk) {
            _CheckForBoolValue(value, [&](const bool b) {
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
                AiRenderSetHintBool(_renderSession, str::progressive, b);
#else
                AiRenderSetHintBool(str::progressive, b);
#endif
                AiNodeSetBool(_options, str::enable_progressive_render, b);
            });
        }
    } else if (key == str::t_progressive_min_AA_samples) {
        if (_context != HdArnoldRenderContext::Husk) {
            _CheckForIntValue(value, [&](const int i) {
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
                AiRenderSetHintInt(_renderSession, str::progressive_min_AA_samples, i);
#else
                AiRenderSetHintInt(str::progressive_min_AA_samples, i);
#endif
            });
        }
    } else if (key == str::t_interactive_target_fps) {
        if (_context != HdArnoldRenderContext::Husk) {
            if (value.IsHolding<float>()) {
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
                AiRenderSetHintFlt(_renderSession, str::interactive_target_fps, value.UncheckedGet<float>());
#else
                AiRenderSetHintFlt(str::interactive_target_fps, value.UncheckedGet<float>());
#endif
            }
        }
    } else if (key == str::t_interactive_target_fps_min) {
        if (_context != HdArnoldRenderContext::Husk) {
            if (value.IsHolding<float>()) {
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
                AiRenderSetHintFlt(_renderSession, str::interactive_target_fps_min, value.UncheckedGet<float>());
#else
                AiRenderSetHintFlt(str::interactive_target_fps_min, value.UncheckedGet<float>());
#endif
            }
        }
    } else if (key == str::t_interactive_fps_min) {
        if (_context != HdArnoldRenderContext::Husk) {
            if (value.IsHolding<float>()) {
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
                AiRenderSetHintFlt(_renderSession, str::interactive_fps_min, value.UncheckedGet<float>());
#else
                AiRenderSetHintFlt(str::interactive_fps_min, value.UncheckedGet<float>());
#endif
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
    } else {
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
    for (auto& productIter : products) {
        HdArnoldDelegateRenderProduct product;
        const auto* productType = TfMapLookupPtr(productIter, _tokens->productType);
        // We only care about deep products for now.
        if (productType == nullptr || !productType->IsHolding<TfToken>() ||
            productType->UncheckedGet<TfToken>() != _tokens->deep) {
            continue;
        }
        // Ignoring cases where productName is not set.
        const auto* productName = TfMapLookupPtr(productIter, _tokens->productName);
        if (productName == nullptr || !productName->IsHolding<TfToken>()) {
            continue;
        }
        product.productName = productName->UncheckedGet<TfToken>();
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
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        AiRenderGetHintBool(_renderSession, str::progressive, v);
#else
        AiRenderGetHintBool(str::progressive, v);
#endif
        return VtValue(v);
    } else if (key == str::t_progressive_min_AA_samples) {
        int v = -4;
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        AiRenderGetHintInt(_renderSession, str::progressive_min_AA_samples, v);
#else
        AiRenderGetHintInt(str::progressive_min_AA_samples, v);
#endif
        return VtValue(v);
    } else if (key == str::t_log_verbosity) {
        return VtValue(_GetLogVerbosityFromFlags(_verbosityLogFlags));
    } else if (key == str::t_log_file) {
        return VtValue(_logFile);
    } else if (key == str::t_interactive_target_fps) {
        float v = 1.0f;
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        AiRenderGetHintFlt(_renderSession, str::interactive_target_fps, v);
#else
        AiRenderGetHintFlt(str::interactive_target_fps, v);
#endif
        return VtValue(v);
    } else if (key == str::t_interactive_target_fps_min) {
        float v = 1.0f;
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        AiRenderGetHintFlt(_renderSession, str::interactive_target_fps_min, v);
#else
        AiRenderGetHintFlt(str::interactive_target_fps_min, v);
#endif
        return VtValue(v);
    } else if (key == str::t_interactive_fps_min) {
        float v = 1.0f;
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        AiRenderGetHintFlt(_renderSession, str::interactive_fps_min, v);
#else
        AiRenderGetHintFlt(str::interactive_fps_min, v);
#endif
        return VtValue(v);
    } else if (key == str::t_profile_file) {
        return VtValue(std::string(AiProfileGetFileName().c_str()));
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
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    AiRenderGetHintFlt(_renderSession, str::total_progress, total_progress);
#else
    AiRenderGetHintFlt(str::total_progress, total_progress);
#endif
    stats[_tokens->percentDone] = total_progress;
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
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdArnoldCamera(this, sprimId);
    }
    if (typeId == HdPrimTypeTokens->material) {
        return new HdArnoldMaterial(this, sprimId);
    }
    if (typeId == HdPrimTypeTokens->sphereLight) {
        return HdArnoldLight::CreatePointLight(this, sprimId);
    }
    if (typeId == HdPrimTypeTokens->distantLight) {
        return HdArnoldLight::CreateDistantLight(this, sprimId);
    }
    if (typeId == HdPrimTypeTokens->diskLight) {
        return HdArnoldLight::CreateDiskLight(this, sprimId);
    }
    if (typeId == HdPrimTypeTokens->rectLight) {
        return HdArnoldLight::CreateRectLight(this, sprimId);
    }
    if (typeId == HdPrimTypeTokens->cylinderLight) {
        return HdArnoldLight::CreateCylinderLight(this, sprimId);
    }
    if (typeId == HdPrimTypeTokens->domeLight) {
        return HdArnoldLight::CreateDomeLight(this, sprimId);
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
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdArnoldCamera(this, SdfPath::EmptyPath());
    }
    if (typeId == HdPrimTypeTokens->material) {
        return new HdArnoldMaterial(this, SdfPath::EmptyPath());
    }
    if (typeId == HdPrimTypeTokens->sphereLight) {
        return HdArnoldLight::CreatePointLight(this, SdfPath::EmptyPath());
    }
    if (typeId == HdPrimTypeTokens->distantLight) {
        return HdArnoldLight::CreateDistantLight(this, SdfPath::EmptyPath());
    }
    if (typeId == HdPrimTypeTokens->diskLight) {
        return HdArnoldLight::CreateDiskLight(this, SdfPath::EmptyPath());
    }
    if (typeId == HdPrimTypeTokens->rectLight) {
        return HdArnoldLight::CreateRectLight(this, SdfPath::EmptyPath());
    }
    if (typeId == HdPrimTypeTokens->cylinderLight) {
        return HdArnoldLight::CreateCylinderLight(this, SdfPath::EmptyPath());
    }
    if (typeId == HdPrimTypeTokens->domeLight) {
        return HdArnoldLight::CreateDomeLight(this, SdfPath::EmptyPath());
    }
    if (typeId == HdPrimTypeTokens->simpleLight) {
        return nullptr;
    }
    if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(SdfPath::EmptyPath());
    }
    TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    return nullptr;
}

void HdArnoldRenderDelegate::DestroySprim(HdSprim* sPrim)
{
    _renderParam->Interrupt();
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

TfToken HdArnoldRenderDelegate::GetMaterialNetworkSelector() const { return _tokens->arnold; }

AtString HdArnoldRenderDelegate::GetLocalNodeName(const AtString& name) const
{
    return AtString(_id.AppendChild(TfToken(name.c_str())).GetText());
}

AtUniverse* HdArnoldRenderDelegate::GetUniverse() const { return _universe; }

#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
AtRenderSession* HdArnoldRenderDelegate::GetRenderSession() const { return _renderSession; }
#endif

AtNode* HdArnoldRenderDelegate::GetOptions() const { return _options; }

AtNode* HdArnoldRenderDelegate::GetFallbackShader() const { return _fallbackShader; }

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

void HdArnoldRenderDelegate::ApplyLightLinking(AtNode* shape, const VtArray<TfToken>& categories)
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

bool HdArnoldRenderDelegate::ShouldSkipIteration(HdRenderIndex* renderIndex, const GfVec2f& shutter)
{
    HdDirtyBits bits = HdChangeTracker::Clean;
    // If Light Linking have changed, we have to dirty the categories on all rprims to force updating the
    // the light linking information.
    if (_lightLinkingChanged.exchange(false, std::memory_order_acq_rel)) {
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
    auto skip = false;
    if (bits != HdChangeTracker::Clean) {
        renderIndex->GetChangeTracker().MarkAllRprimsDirty(bits);
        skip = true;
    }
    SdfPath id;
    // We are first removing materials, to avoid untracking them later.
    while (_materialRemovalQueue.try_pop(id)) {
        _materialToShapeMap.erase(id);
    }
    ShapeMaterialChange shapeChange;
    while (_shapeMaterialUntrackQueue.try_pop(shapeChange)) {
        for (const auto& material : shapeChange.materials) {
            auto it = _materialToShapeMap.find(material);
            // In case it was already removed.
            if (it != _materialToShapeMap.end()) {
                it->second.erase(shapeChange.shape);
            }
        }
    }
    while (_shapeMaterialTrackQueue.try_pop(shapeChange)) {
        for (const auto& material : shapeChange.materials) {
            auto it = _materialToShapeMap.find(material);
            if (it == _materialToShapeMap.end()) {
                _materialToShapeMap.insert({material, {shapeChange.shape}});
            } else {
                it->second.insert(shapeChange.shape);
            }
        }
    }
    auto& changeTracker = renderIndex->GetChangeTracker();
    // And at last we are triggering changes.
    while (_materialDirtyQueue.try_pop(id)) {
        auto it = _materialToShapeMap.find(id);
        // There could be cases where the material is not assigned anything tracking, but this should be rare.
        if (it != _materialToShapeMap.end()) {
            skip = true;
            for (const auto& shape : it->second) {
                changeTracker.MarkRprimDirty(shape, HdChangeTracker::DirtyMaterialId);
            }
        }
    }
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

void HdArnoldRenderDelegate::DirtyMaterial(const SdfPath& id) { _materialDirtyQueue.emplace(id); }

void HdArnoldRenderDelegate::RemoveMaterial(const SdfPath& id) { _materialRemovalQueue.emplace(id); }

void HdArnoldRenderDelegate::TrackShapeMaterials(const SdfPath& shape, const VtArray<SdfPath>& materials)
{
    _shapeMaterialTrackQueue.emplace(shape, materials);
}

void HdArnoldRenderDelegate::UntrackShapeMaterials(const SdfPath& shape, const VtArray<SdfPath>& materials)
{
    _shapeMaterialUntrackQueue.emplace(shape, materials);
}

void HdArnoldRenderDelegate::TrackRenderTag(AtNode* node, const TfToken& tag)
{
    AiNodeSetDisabled(node, std::find(_renderTags.begin(), _renderTags.end(), tag) == _renderTags.end());
    _renderTagTrackQueue.push({node, tag});
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

PXR_NAMESPACE_CLOSE_SCOPE
