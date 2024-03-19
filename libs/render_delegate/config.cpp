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
#include "config.h"

#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/instantiateSingleton.h>

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(HdArnoldConfig);

TF_DEFINE_ENV_SETTING(HDARNOLD_bucket_size, 24, "Bucket size.");

TF_DEFINE_ENV_SETTING(HDARNOLD_abort_on_error, false, "Abort on error.");

TF_DEFINE_ENV_SETTING(HDARNOLD_log_verbosity, 2, "Control the amount of log output. (0-5)");

TF_DEFINE_ENV_SETTING(HDARNOLD_log_file, "", "Set a filepath to output logging information to.");

// These two are "secret", in the sense that they're not exposed via
// HdArnoldRenderDelegate::GetRenderSettingDescriptors, as they would be too confusing /
// advanced to expose via a GUI to artists.  However, they're settable via env vars if you
// really need exact control. See ai_msg.h for possible values / flags.
TF_DEFINE_ENV_SETTING(HDARNOLD_log_flags_console, -1, "Override logging flags for console output, if non-negative.");

TF_DEFINE_ENV_SETTING(HDARNOLD_log_flags_file, -1, "Override logging flags for file output, if non-negative.");

TF_DEFINE_ENV_SETTING(HDARNOLD_AA_samples, 3, "Number of AA samples by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_GI_diffuse_samples, 2, "Number of diffuse samples by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_GI_specular_samples, 2, "Number of specular samples by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_GI_transmission_samples, 2, "Number of transmission samples by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_GI_sss_samples, 2, "Number of sss samples by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_GI_volume_samples, 2, "Number of volume samples by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_threads, -1, "Number of Threads for CPU rendering by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_GI_diffuse_depth, 1, "Diffuse ray depth by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_GI_specular_depth, 1, "Diffuse ray depth by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_GI_transmission_depth, 8, "Transmission ray depth by default.");

TF_DEFINE_ENV_SETTING(HDARNOLD_enable_progressive_render, true, "Enable progressive render.");

TF_DEFINE_ENV_SETTING(HDARNOLD_progressive_min_AA_samples, -4, "Minimum AA samples for progressive rendering.");

TF_DEFINE_ENV_SETTING(HDARNOLD_enable_adaptive_sampling, false, "Enable adaptive sapmling.");

TF_DEFINE_ENV_SETTING(HDARNOLD_enable_gpu_rendering, false, "Enable gpu rendering.");

// This macro doesn't support floating point values.
TF_DEFINE_ENV_SETTING(HDARNOLD_shutter_start, "-0.25f", "Shutter start for the camera.");

TF_DEFINE_ENV_SETTING(HDARNOLD_shutter_end, "0.25f", "Shutter end for the camera.");

TF_DEFINE_ENV_SETTING(HDARNOLD_interactive_target_fps, "30.0", "Interactive target fps for progressive rendering.");

TF_DEFINE_ENV_SETTING(
    HDARNOLD_interactive_target_fps_min, "20.0", "Min interactive target fps for progressive rendering.");

TF_DEFINE_ENV_SETTING(HDARNOLD_interactive_fps_min, "5.0", "Minimum fps for progressive rendering.");

TF_DEFINE_ENV_SETTING(HDARNOLD_profile_file, "", "Output file for profiling information.");

TF_DEFINE_ENV_SETTING(HDARNOLD_texture_searchpath, "", "Texture search path.");

TF_DEFINE_ENV_SETTING(HDARNOLD_plugin_searchpath, "", "Plugin search path.");

TF_DEFINE_ENV_SETTING(HDARNOLD_procedural_searchpath, "", "Procedural search path.");

TF_DEFINE_ENV_SETTING(HDARNOLD_osl_includepath, "", "OSL include path.");

TF_DEFINE_ENV_SETTING(HDARNOLD_auto_generate_tx, true, "Auto-generate Textures to TX");

HdArnoldConfig::HdArnoldConfig()
{
    bucket_size = std::max(1, TfGetEnvSetting(HDARNOLD_bucket_size));
    abort_on_error = TfGetEnvSetting(HDARNOLD_abort_on_error);
    log_verbosity = std::max(0, std::min(7, TfGetEnvSetting(HDARNOLD_log_verbosity)));
    log_file = TfGetEnvSetting(HDARNOLD_log_file);
    log_flags_console = TfGetEnvSetting(HDARNOLD_log_flags_console);
    log_flags_file = TfGetEnvSetting(HDARNOLD_log_flags_file);
    threads = TfGetEnvSetting(HDARNOLD_threads);
    AA_samples = TfGetEnvSetting(HDARNOLD_AA_samples);
    GI_diffuse_samples = std::max(0, TfGetEnvSetting(HDARNOLD_GI_diffuse_samples));
    GI_specular_samples = std::max(0, TfGetEnvSetting(HDARNOLD_GI_specular_samples));
    GI_transmission_samples = std::max(0, TfGetEnvSetting(HDARNOLD_GI_transmission_samples));
    GI_sss_samples = std::max(0, TfGetEnvSetting(HDARNOLD_GI_sss_samples));
    GI_volume_samples = std::max(0, TfGetEnvSetting(HDARNOLD_GI_volume_samples));
    GI_diffuse_depth = std::max(0, TfGetEnvSetting(HDARNOLD_GI_diffuse_depth));
    GI_specular_depth = std::max(0, TfGetEnvSetting(HDARNOLD_GI_specular_depth));
    GI_transmission_depth = std::max(0, TfGetEnvSetting(HDARNOLD_GI_transmission_depth));
    enable_progressive_render = TfGetEnvSetting(HDARNOLD_enable_progressive_render);
    progressive_min_AA_samples = TfGetEnvSetting(HDARNOLD_progressive_min_AA_samples);
    enable_adaptive_sampling = TfGetEnvSetting(HDARNOLD_enable_adaptive_sampling);
    enable_gpu_rendering = TfGetEnvSetting(HDARNOLD_enable_gpu_rendering);
    shutter_start = static_cast<float>(std::atof(TfGetEnvSetting(HDARNOLD_shutter_start).c_str()));
    shutter_end = static_cast<float>(std::atof(TfGetEnvSetting(HDARNOLD_shutter_end).c_str()));
    interactive_target_fps =
        std::max(1.0f, static_cast<float>(std::atof(TfGetEnvSetting(HDARNOLD_interactive_target_fps).c_str())));
    interactive_target_fps_min =
        std::max(1.0f, static_cast<float>(std::atof(TfGetEnvSetting(HDARNOLD_interactive_target_fps_min).c_str())));
    interactive_fps_min =
        std::max(1.0f, static_cast<float>(std::atof(TfGetEnvSetting(HDARNOLD_interactive_fps_min).c_str())));
    profile_file = TfGetEnvSetting(HDARNOLD_profile_file);
    texture_searchpath = TfGetEnvSetting(HDARNOLD_texture_searchpath);
    plugin_searchpath = TfGetEnvSetting(HDARNOLD_plugin_searchpath);
    procedural_searchpath = TfGetEnvSetting(HDARNOLD_procedural_searchpath);
    osl_includepath = TfGetEnvSetting(HDARNOLD_osl_includepath);
    auto_generate_tx = TfGetEnvSetting(HDARNOLD_auto_generate_tx);
}

const HdArnoldConfig& HdArnoldConfig::GetInstance() { return TfSingleton<HdArnoldConfig>::GetInstance(); }

PXR_NAMESPACE_CLOSE_SCOPE
