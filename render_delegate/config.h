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
/// @file config.h
///
/// Configuration settings for the Render Delegate.
///
/// Access configuration settings not available through the public interface.
#pragma once

#include <pxr/pxr.h>

#include <pxr/base/tf/singleton.h>

#include "api.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Class that holds the global configuration values for the Render Delegate.
///
/// Note: we are not following the coding conventions for the members, as we want
/// to match the Arnold parameter names, which are snake_case.
struct HdArnoldConfig {
    /// Return an instance of HdArnoldConfig.
    HDARNOLD_API
    static const HdArnoldConfig& GetInstance();

    HdArnoldConfig(const HdArnoldConfig&) = delete;
    HdArnoldConfig(HdArnoldConfig&&) = delete;
    HdArnoldConfig& operator=(const HdArnoldConfig&) = delete;

    /// Use HDARNOLD_bucket_size to set the value.
    ///
    int bucket_size; ///< Bucket size for non-progressive renders.

    /// Use HDARNOLD_abort_on_error to set the value.
    ///
    bool abort_on_error; ///< Abort render if any errors occur.

    /// Use HDARNOLD_log_verbosity to set the value.
    ///
    int log_verbosity; ///< Control how many messages are output (0-5).

    /// Use HDARNOLD_log_file to set the value.
    ///
    std::string log_file; ///< Set a filepath to output logging information to.

    /// Use HDARNOLD_log_flags_console to set the value.
    ///
    int log_flags_console; ///< Override logging flags for console output.

    /// Use HDARNOLD_log_flags_file to set the value.
    ///
    int log_flags_file; ///< Override logging flags for file output.

    /// Use HDARNOLD_threads to set the value.
    ///
    int threads; ///< Number of threads to use for CPU rendering.

    /// Use HDARNOLD_GI_diffuse_samples to set the value.
    ///
    int GI_diffuse_samples; ///< Number of diffuse samples.

    /// Use HDARNOLD_GI_specular_samples to set the value.
    ///
    int GI_specular_samples; ///< Number of specular samples.

    /// Use HDARNOLD_GI_transmission_samples to set the value.
    ///
    int GI_transmission_samples; ///< Number of transmission samples.

    /// Use HDARNOLD_GI_sss_samples to set the value.
    ///
    int GI_sss_samples; ///< Number of sss samples.

    /// Use HDARNOLD_GI_volume_samples to set the value.
    ///
    int GI_volume_samples; ///< Number of volume samples.

    /// Use HDARNOLD_AA_samples to set the value.
    ///
    int AA_samples; ///< Initial setting for AA samples.

    /// Use HDARNOLD_GI_diffuse_depth to set the value.
    ///
    int GI_diffuse_depth; ///< Initial setting for Diffuse Depth.

    /// Use HDARNOLD_GI_specular_depth to set the value.
    ///
    int GI_specular_depth; ///< Initial setting for Specular Depth.

    /// Use HDARNOLD_enable_progressive_render to set the value.
    ///
    bool enable_progressive_render; ///< Enables progressive rendering.

    /// Use HDARNOLD_progressive_min_AA_samples to set the value.
    ///
    int progressive_min_AA_samples;

    /// Use HDARNOLD_enable_adaptive_sampling to set the value.
    ///
    bool enable_adaptive_sampling; ///< Enables adaptive sampling.

    /// Use HDARNOLD_enable_gpu_rendering to set the value.
    ///
    bool enable_gpu_rendering; ///< Enables gpu rendering.

    /// Use HDARNOLD_shutter_start to set the value.
    ///
    float shutter_start; ///< Shutter start for the camera.

    /// Use HDARNOLD_shutter_end to set the value.
    ///
    float shutter_end; ///< Shutter end for the camera.

    /// Use HDARNOLD_interactive_target_fps to set the value.
    ///
    float interactive_target_fps; ///< Interactive Target FPS.

    /// Use HDARNOLD_interactive_target_fps_min to set the value.
    ///
    float interactive_target_fps_min; ///< Interactive Target FPS Minimum.

    /// Use HDARNOLD_interactive_fps_min to set value.
    ///
    float interactive_fps_min; ///< Interactive FPS Minimum.

    /// Use HDARNOLD_profile_file to set the value.
    ///
    std::string profile_file; ///< Output file for profiling data.

private:
    /// Constructor for reading the values from the environment variables.
    HDARNOLD_API
    HdArnoldConfig();
    ~HdArnoldConfig() = default;

    friend class TfSingleton<HdArnoldConfig>;
};

PXR_NAMESPACE_CLOSE_SCOPE
