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
/// @file render_param.h
///
/// Utilities to control the flow of rendering.
#pragma once

#include "api.h"

#include <pxr/pxr.h>

#include <pxr/imaging/hd/renderDelegate.h>

#include <atomic>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class to control the flow of rendering.
class HdArnoldRenderParam final : public HdRenderParam {
public:
    /// Rendering status.
    enum class Status {
        Converging, ///< Render is still converging.
        Converged,  ///< Render converged.
        Aborted     ///< Render aborted.
    };

    /// Constructor for HdArnoldRenderParam.
    HdArnoldRenderParam();

    /// Destructor for HdArnoldRenderParam.
    ~HdArnoldRenderParam() override = default;

    /// Starts or continues rendering.
    ///
    /// Function to start rendering or resume rendering if it has ended. Returns
    /// true if the render finished, false otherwise.
    ///
    /// @return True if Arnold Core has finished converging.
    HDARNOLD_API
    Status Render();
    /// Interrupts an ongoing render.
    ///
    /// Useful when there is new data to display, or the render settings have changed.
    ///
    /// @param needsRestart Whether or not changes are applied to the scene and we need to restart rendering.
    /// @param clearStatus Clears the internal failure status. Set it to false when no scene data changed, that could
    ///  affect the aborted internal status.
    HDARNOLD_API
    void Interrupt(bool needsRestart = true, bool clearStatus = true);
    /// Pauses an ongoing render, does nothing if no render is running.
    HDARNOLD_API
    void Pause();
    /// Resumes an already paused render, does nothing if no render is running, or the render is not paused.
    HDARNOLD_API
    void Resume();
    /// Resumes an already running,stopped/paused/finished render.
    HDARNOLD_API
    void Restart();

private:
    /// Indicate if render needs restarting, in case interrupt is called after rendering has finished.
    std::atomic<bool> _needsRestart;
    /// Indicate if rendering has been aborted at one point or another.
    std::atomic<bool> _aborted;
    /// Indicate if rendering has been paused.
    std::atomic<bool> _paused;
};

class HdArnoldRenderParamInterrupt {
public:
    /// Constructor for HdArnoldRenderParamInterrupt.
    ///
    /// @param param Pointer to the HdRenderParam struct.
    HdArnoldRenderParamInterrupt(HdRenderParam* param) : _param(reinterpret_cast<HdArnoldRenderParam*>(param)) {}

    /// Interrupts an ongoing render.
    ///
    /// Only calls interrupt once per created instance of HdArnoldRenderParamInterrupt.
    void Interrupt()
    {
        if (!_hasInterrupted) {
            _hasInterrupted = true;
            _param->Interrupt();
        }
    }

private:
    /// Indicate if the render has been interrupted already.
    bool _hasInterrupted = false;
    /// Pointer to the Arnold Render Param struct held inside.
    HdArnoldRenderParam* _param = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE
