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

#include "config.h"
#include "constant_strings.h"
#include "instancer.h"
#include "light.h"
#include "material.h"
#include "mesh.h"
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
);
// clang-format on

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
        auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, key.GetText());
        if (paramEntry != nullptr) {
            const auto paramType = AiParamGetType(paramEntry);
            if (paramType == AI_TYPE_INT) {
                AiNodeSetInt(node, key.GetText(), value.UncheckedGet<int>());
            } else if (paramType == AI_TYPE_BOOLEAN) {
                AiNodeSetBool(node, key.GetText(), value.UncheckedGet<int>() != 0);
            }
        }
        // Or longs.
    } else if (value.IsHolding<long>()) {
        const auto* nodeEntry = AiNodeGetNodeEntry(node);
        auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, key.GetText());
        if (paramEntry != nullptr) {
            const auto paramType = AiParamGetType(paramEntry);
            if (paramType == AI_TYPE_INT) {
                AiNodeSetInt(node, key.GetText(), static_cast<int>(value.UncheckedGet<long>()));
            } else if (paramType == AI_TYPE_BOOLEAN) {
                AiNodeSetBool(node, key.GetText(), value.UncheckedGet<long>() != 0);
            }
        }
        // Or long longs.
    } else if (value.IsHolding<long long>()) {
        const auto* nodeEntry = AiNodeGetNodeEntry(node);
        auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, key.GetText());
        if (paramEntry != nullptr) {
            const auto paramType = AiParamGetType(paramEntry);
            if (paramType == AI_TYPE_INT) {
                AiNodeSetInt(node, key.GetText(), static_cast<int>(value.UncheckedGet<long long>()));
            } else if (paramType == AI_TYPE_BOOLEAN) {
                AiNodeSetBool(node, key.GetText(), value.UncheckedGet<long long>() != 0);
            }
        }
    } else if (value.IsHolding<float>()) {
        AiNodeSetFlt(node, key.GetText(), value.UncheckedGet<float>());
    } else if (value.IsHolding<double>()) {
        AiNodeSetFlt(node, key.GetText(), static_cast<float>(value.UncheckedGet<double>()));
    } else if (value.IsHolding<bool>()) {
        AiNodeSetBool(node, key.GetText(), value.UncheckedGet<bool>());
    } else if (value.IsHolding<std::string>()) {
        AiNodeSetStr(node, key.GetText(), value.UncheckedGet<std::string>().c_str());
    } else if (value.IsHolding<TfToken>()) {
        AiNodeSetStr(node, key.GetText(), value.UncheckedGet<TfToken>().GetText());
    }
}

inline const TfTokenVector& _SupportedRprimTypes()
{
    static const TfTokenVector r{HdPrimTypeTokens->mesh, HdPrimTypeTokens->volume, HdPrimTypeTokens->points};
    return r;
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
    }
}

template <typename F>
void _CheckForIntValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<int>()) {
        f(value.UncheckedGet<int>());
    } else if (value.IsHolding<long>()) {
        f(static_cast<int>(value.UncheckedGet<long>()));
    }
}

inline TfToken _RemoveArnoldGlobalPrefix(const TfToken& key)
{
    return TfStringStartsWith(key, _tokens->arnoldGlobal) ? TfToken{key.GetText() + _tokens->arnoldGlobal.size()} : key;
}

} // namespace

std::mutex HdArnoldRenderDelegate::_mutexResourceRegistry;
std::atomic_int HdArnoldRenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdArnoldRenderDelegate::_resourceRegistry;

HdArnoldRenderDelegate::HdArnoldRenderDelegate()
{
    _id = SdfPath(TfToken(TfStringPrintf("/HdArnoldRenderDelegate_%p", this)));
    if (AiUniverseIsActive()) {
        TF_CODING_ERROR("There is already an active Arnold universe!");
    }
    AiBegin(AI_SESSION_INTERACTIVE);
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
    const auto arnoldPluginPath = TfGetenv("ARNOLD_PLUGIN_PATH");
    if (!arnoldPluginPath.empty()) {
        AiLoadPlugins(arnoldPluginPath.c_str());
    }

    _universe = nullptr;

    _options = AiUniverseGetOptions(_universe);
    for (const auto& o : _GetSupportedRenderSettings()) {
        _SetRenderSetting(o.first, o.second.defaultValue);
    }

    _fallbackShader = AiNode(_universe, "utility");
    AiNodeSetStr(_fallbackShader, str::name, TfStringPrintf("fallbackShader_%p", _fallbackShader).c_str());
    AiNodeSetStr(_fallbackShader, "shade_mode", "ambocc");
    AiNodeSetStr(_fallbackShader, "color_mode", "color");
    auto* userDataReader = AiNode(_universe, "user_data_rgb");
    AiNodeSetStr(userDataReader, str::name, TfStringPrintf("fallbackShader_userDataReader_%p", userDataReader).c_str());
    AiNodeSetStr(userDataReader, "attribute", "displayColor");
    AiNodeSetRGB(userDataReader, "default", 1.0f, 1.0f, 1.0f);
    AiNodeLink(userDataReader, "color", _fallbackShader);

    _fallbackVolumeShader = AiNode(_universe, "standard_volume");
    AiNodeSetStr(_fallbackVolumeShader, str::name, TfStringPrintf("fallbackVolume_%p", _fallbackVolumeShader).c_str());

    _renderParam.reset(new HdArnoldRenderParam());

    // AiRenderSetHintBool(str::progressive, true);
    // We need access to both beauty and P at the same time.
    AiRenderSetHintBool(str::progressive_show_all_outputs, true);
}

HdArnoldRenderDelegate::~HdArnoldRenderDelegate()
{
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
    if (_counterResourceRegistry.fetch_sub(1) == 1) {
        _resourceRegistry.reset();
    }
    _renderParam->Interrupt();
    hdArnoldUninstallNodes();
    AiUniverseDestroy(_universe);
    AiEnd();
}

HdRenderParam* HdArnoldRenderDelegate::GetRenderParam() const { return _renderParam.get(); }

void HdArnoldRenderDelegate::CommitResources(HdChangeTracker* tracker) { TF_UNUSED(tracker); }

const TfTokenVector& HdArnoldRenderDelegate::GetSupportedRprimTypes() const { return _SupportedRprimTypes(); }

const TfTokenVector& HdArnoldRenderDelegate::GetSupportedSprimTypes() const { return _SupportedSprimTypes(); }

const TfTokenVector& HdArnoldRenderDelegate::GetSupportedBprimTypes() const { return _SupportedBprimTypes(); }

void HdArnoldRenderDelegate::_SetRenderSetting(const TfToken& _key, const VtValue& _value)
{
    const auto key = _RemoveArnoldGlobalPrefix(_key);

    // Currently usdview can return double for floats, so until it's fixed
    // we have to convert doubles to float.
    auto value = _value.IsHolding<double>() ? VtValue(static_cast<float>(_value.UncheckedGet<double>())) : _value;
    // Certain applications might pass boolean values via ints or longs.
    if (key == str::t_enable_gpu_rendering) {
        _CheckForBoolValue(value, [&](const bool b) {
            AiNodeSetStr(_options, str::render_device, b ? str::GPU : str::CPU);
            AiDeviceAutoSelect();
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
        _CheckForBoolValue(value, [&](const bool b) {
            AiRenderSetHintBool(str::progressive, b);
            AiNodeSetBool(_options, str::enable_progressive_render, b);
        });
    } else if (key == str::t_progressive_min_AA_samples) {
        _CheckForIntValue(value, [&](const int i) { AiRenderSetHintInt(str::progressive_min_AA_samples, i); });
    } else if (key == str::t_interactive_target_fps) {
        if (value.IsHolding<float>()) {
            AiRenderSetHintFlt(str::interactive_target_fps, value.UncheckedGet<float>());
        }
    } else if (key == str::t_interactive_target_fps_min) {
        if (value.IsHolding<float>()) {
            AiRenderSetHintFlt(str::interactive_target_fps_min, value.UncheckedGet<float>());
        }
    } else if (key == str::t_interactive_fps_min) {
        if (value.IsHolding<float>()) {
            AiRenderSetHintFlt(str::interactive_fps_min, value.UncheckedGet<float>());
        }
    } else if (key == str::t_profile_file) {
        if (value.IsHolding<std::string>()) {
            AiProfileSetFileName(value.UncheckedGet<std::string>().c_str());
        }
    } else {
        auto* optionsEntry = AiNodeGetNodeEntry(_options);
        // Sometimes the Render Delegate receives parameters that don't exist
        // on the options node. For example, if the host application ignores the
        // render setting descriptor list.
        if (AiNodeEntryLookUpParameter(optionsEntry, key.GetText()) != nullptr) {
            _SetNodeParam(_options, key, value);
        }
    }
}

void HdArnoldRenderDelegate::SetRenderSetting(const TfToken& key, const VtValue& value)
{
    _renderParam->Interrupt();
    _SetRenderSetting(key, value);
}

VtValue HdArnoldRenderDelegate::GetRenderSetting(const TfToken& _key) const
{
    const auto key = _RemoveArnoldGlobalPrefix(_key);

    if (key == str::t_enable_gpu_rendering) {
        return VtValue(AiNodeGetStr(_options, str::render_device) == str::GPU);
    } else if (key == str::t_enable_progressive_render) {
        bool v = true;
        AiRenderGetHintBool(str::progressive, v);
        return VtValue(v);
    } else if (key == str::t_progressive_min_AA_samples) {
        int v = -4;
        AiRenderGetHintInt(str::progressive_min_AA_samples, v);
        return VtValue(v);
    } else if (key == str::t_log_verbosity) {
        return VtValue(_GetLogVerbosityFromFlags(_verbosityLogFlags));
    } else if (key == str::t_log_file) {
        return VtValue(_logFile);
    } else if (key == str::t_interactive_target_fps) {
        float v = 1.0f;
        AiRenderGetHintFlt(str::interactive_target_fps, v);
        return VtValue(v);
    } else if (key == str::t_interactive_target_fps_min) {
        float v = 1.0f;
        AiRenderGetHintFlt(str::interactive_target_fps_min, v);
        return VtValue(v);
    } else if (key == str::t_interactive_fps_min) {
        float v = 1.0f;
        AiRenderGetHintFlt(str::interactive_fps_min, v);
        return VtValue(v);
    } else if (key == str::t_profile_file) {
        return VtValue(std::string(AiProfileGetFileName().c_str()));
    }
    const auto* nentry = AiNodeGetNodeEntry(_options);
    const auto* pentry = AiNodeEntryLookUpParameter(nentry, key.GetText());
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
            const auto* pentry = AiNodeEntryLookUpParameter(nentry, it.first.GetText());
            desc.defaultValue = _GetNodeParamValue(_options, pentry);
        } else {
            desc.defaultValue = it.second.defaultValue;
        }
        ret.emplace_back(std::move(desc));
    }
    return ret;
}

HdResourceRegistrySharedPtr HdArnoldRenderDelegate::GetResourceRegistry() const { return _resourceRegistry; }

HdRenderPassSharedPtr HdArnoldRenderDelegate::CreateRenderPass(
    HdRenderIndex* index, const HdRprimCollection& collection)
{
    return HdRenderPassSharedPtr(new HdArnoldRenderPass(this, index, collection));
}

HdInstancer* HdArnoldRenderDelegate::CreateInstancer(
    HdSceneDelegate* delegate, const SdfPath& id, const SdfPath& instancerId)
{
    return new HdArnoldInstancer(this, delegate, id, instancerId);
}

void HdArnoldRenderDelegate::DestroyInstancer(HdInstancer* instancer) { delete instancer; }

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
    TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
    return nullptr;
}

void HdArnoldRenderDelegate::DestroyRprim(HdRprim* rPrim)
{
    _renderParam->Interrupt();
    delete rPrim;
}

HdSprim* HdArnoldRenderDelegate::CreateSprim(const TfToken& typeId, const SdfPath& sprimId)
{
    _renderParam->Interrupt();
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(sprimId);
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
        // return HdArnoldLight::CreateSimpleLight(this, sprimId);
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
        return new HdCamera(SdfPath::EmptyPath());
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
        // return HdArnoldLight::CreateSimpleLight(this, SdfPath::EmptyPath());
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

void HdArnoldRenderDelegate::DestroyBprim(HdBprim* bPrim) { delete bPrim; }

TfToken HdArnoldRenderDelegate::GetMaterialBindingPurpose() const { return HdTokens->full; }

TfToken HdArnoldRenderDelegate::GetMaterialNetworkSelector() const { return _tokens->arnold; }

AtString HdArnoldRenderDelegate::GetLocalNodeName(const AtString& name) const
{
    return AtString(_id.AppendChild(TfToken(name.c_str())).GetText());
}

AtUniverse* HdArnoldRenderDelegate::GetUniverse() const { return _universe; }

AtNode* HdArnoldRenderDelegate::GetOptions() const { return _options; }

AtNode* HdArnoldRenderDelegate::GetFallbackShader() const { return _fallbackShader; }

AtNode* HdArnoldRenderDelegate::GetFallbackVolumeShader() const { return _fallbackVolumeShader; }

HdAovDescriptor HdArnoldRenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    if (name == HdAovTokens->color) {
        return HdAovDescriptor(HdFormatUNorm8Vec4, false, VtValue(GfVec4f(0.0f)));
    } else if (name == HdAovTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
    } else if (name == HdAovTokens->primId) {
        return HdAovDescriptor(HdFormatInt32, false, VtValue(-1));
    }

    return HdAovDescriptor();
}

PXR_NAMESPACE_CLOSE_SCOPE
