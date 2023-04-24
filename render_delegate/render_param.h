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
/// @file render_param.h
///
/// Utilities to control the flow of rendering.
#pragma once

#include "api.h"

#include <pxr/pxr.h>

#include <pxr/base/gf/vec2f.h>

#include <pxr/imaging/hd/renderDelegate.h>

#include <ai.h>

#include "hdarnold.h"

#include <atomic>
#include <chrono>

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldRenderDelegate;

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
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    HdArnoldRenderParam(HdArnoldRenderDelegate* delegate);
#else
    HdArnoldRenderParam();
#endif
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

    /// Gets the shutter range.
    ///
    /// @return Constant reference to the shutter range.
    const GfVec2f& GetShutterRange() const { return _shutter; }

    /// Gets the FPS.
    ///
    /// @return Constant reference to the FPS.
    const float& GetFPS() const { return _fps; }

    /// Tells if shutter is instananeous.
    ///
    /// @return True if shutter range is zero, false otherwise.
    bool InstananeousShutter() const { return GfIsClose(_shutter[0], _shutter[1], AI_EPSILON); }

    /// Updates the shutter range.
    ///
    /// @param shutter New shutter range to use.
    /// @return True if shutter range has changed.
    bool UpdateShutter(const GfVec2f& shutter);

    /// Updates the FPS.
    ///
    /// @param FPS New FPS to use.
    /// @return True if FPS has changed.
    bool UpdateFPS(const float FPS);

    /// For debugging purpose, allow to save out the 
    /// Arnold scene to a file, just before it's rendered
    void WriteDebugScene() const;

    /// Enable the AiMsg callback
    void StartRenderMsgLog();

    /// Disable the AiMsg callback
    void StopRenderMsgLog();

    /// Restart the AiMsg callback
    void RestartRenderMsgLog();

    /// Used by the AiMsg callback to cache the render status
    void CacheLogMessage(const char* msgString, int severity);

    /// Retrieve the last Arnold status message (threadsafe)
    ///
    /// @return render details, i.e. 'Rendering' or '[gpu] compiling shaders'
    std::string GetRenderStatusString() const;

    /// Calculates the total render time. This will reset if the scene is dirtied (i.e. tthe camera changes)
    ///
    /// @return elapsed render time in ms
    double GetElapsedRenderTime() const;

private:
    inline void ResetStartTimer()
    {
        _renderStartTime.store(std::chrono::system_clock::now(), std::memory_order::memory_order_release);
    }

#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    /// The render delegate
    const HdArnoldRenderDelegate* _delegate;
#endif
    /// Indicate if render needs restarting, in case interrupt is called after rendering has finished.
    std::atomic<bool> _needsRestart;
    /// Indicate if rendering has been aborted at one point or another.
    std::atomic<bool> _aborted;
    /// Indicate if rendering has been paused.
    std::atomic<bool> _paused;

    std::atomic<std::chrono::time_point<std::chrono::system_clock>> _renderStartTime;

    unsigned int _msgLogCallback;
    std::string _logMsg;
    mutable std::mutex _logMutex;

    /// Shutter range.
    GfVec2f _shutter = {0.0f, 0.0f};
    /// FPS.
    float _fps = 24.0f;
    /// optionally save out the arnold scene to a file, before it's rendered
    std::string _debugScene;
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

    /// Returns a constant pointer to HdArnoldRenderParam.
    ///
    /// @return Const pointer to HdArnoldRenderParam.
    const HdArnoldRenderParam* operator()() const { return _param; }

    /// Returns a pointer to HdArnoldRenderParam.
    ///
    /// @return Pointer to HdArnoldRenderParam.
    HdArnoldRenderParam* operator()() { return _param; }

private:
    /// Indicate if the render has been interrupted already.
    bool _hasInterrupted = false;
    /// Pointer to the Arnold Render Param struct held inside.
    HdArnoldRenderParam* _param = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE
