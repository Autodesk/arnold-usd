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

#include <chrono>
#include <mutex>

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
    HdArnoldRenderParam(HdArnoldRenderDelegate* delegate);
    /// Destructor for HdArnoldRenderParam. Must explicitly stop the message-log
    /// callback so the registration is removed from Arnold's global table —
    /// the defaulted destructor would leak the callback slot otherwise.
    HDARNOLD_API
    ~HdArnoldRenderParam() override;

    /// Starts or continues rendering.
    ///
    /// Function to start rendering or resume rendering if it has ended. Returns
    /// true if the render finished, false otherwise.
    ///
    /// @return True if Arnold Core has finished converging.
    HDARNOLD_API
    Status UpdateRender();
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

    /// Retrieve the last Arnold status message (threadsafe)
    ///
    /// @return render details, i.e. 'Rendering' or '[gpu] compiling shaders'
    std::string GetRenderStatusString() const;

    /// Updates the per-instance cached log message from the Arnold message
    /// callback. Public so the file-scope `_MsgStatusCallback` free function can
    /// route into the correct instance via its userPtr argument.
    void SetCachedLogMessage(const char* msg);

    /// Calculates the total render time. This will reset if the scene is dirtied (i.e. tthe camera changes)
    ///
    /// @return elapsed render time in ms
    double GetElapsedRenderTime() const;

    /// Returns the latest render error code.
    ///
    /// @return error code.
    AtRenderErrorCode GetErrorCode() const {return _errorCode;};

    /// Sets the path to the driving hydra render settings prim.
    ///
    /// @param path Path to the render settings prim.
    HDARNOLD_API
    void SetHydraRenderSettingsPrimPath(SdfPath const &path);

    /// Gets the path to the driving hydra render settings prim.
    ///
    /// @return Constant reference to the render settings prim path.
    HDARNOLD_API
    const SdfPath& GetHydraRenderSettingsPrimPath() const;

private:
    inline void ResetStartTimer()
    {
        // Use std::lock_guard so the mutex is released even if the time_point
        // assignment or system_clock::now() throws. The previous manual
        // lock()/unlock() would deadlock every future caller of any method
        // protected by _renderTimeMutex if an exception escaped.
        std::lock_guard<std::mutex> guard(_renderTimeMutex);
        _renderStartTime = std::chrono::system_clock::now();
    }

    /// The render delegate
    const HdArnoldRenderDelegate* _delegate;
    /// Indicate if render needs restarting, in case interrupt is called after rendering has finished.
    std::atomic<bool> _needsRestart{false};
    /// Indicate if rendering has been aborted at one point or another.
    std::atomic<bool> _aborted{false};
    /// Indicate if rendering has been paused. Prior to C++20 std::atomic<bool>'s
    /// default constructor leaves the value indeterminate, and the cpp file's
    /// constructor explicitly stores false into _needsRestart and _aborted but
    /// not into _paused — so the first UpdateRender call read uninitialized
    /// memory. Initialize all three here for safety.
    std::atomic<bool> _paused{false};

    std::chrono::time_point<std::chrono::system_clock> _renderStartTime;
    mutable std::mutex _renderTimeMutex;

    int _msgLogCallback = -1;

    /// Per-instance cached log message and its mutex. These were previously
    /// file-scope globals shared across every HdArnoldRenderParam instance, so
    /// concurrent renders (multiple delegates) overwrote each other's status
    /// strings and GetRenderStatusString could return a message that came from
    /// a different instance entirely.
    mutable std::mutex _cachedLogMutex;
    std::string _cachedLogMsg;

    /// Shutter range.
    GfVec2f _shutter = {0.0f, 0.0f};
    /// FPS.
    float _fps = 24.0f;
    /// optionally save out the arnold scene to a file, before it's rendered
    std::string _debugScene;
    /// Arnold error code
    AtRenderErrorCode _errorCode = AI_SUCCESS;
    /// Path to the driving hydra render settings prim.
    SdfPath _hydraRenderSettingsPrimPath;
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
        // _param is initialized in the constructor via reinterpret_cast from the
        // HdRenderParam* the caller passes in. If the caller passes nullptr
        // (legal at the HdRenderParam* interface), _param is also nullptr and
        // dereferencing it crashes. Guard against that here.
        if (_param != nullptr && !_hasInterrupted) {
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
