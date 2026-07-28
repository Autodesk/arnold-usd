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
#include "render_param.h"
#include "render_delegate.h"
#include <constant_strings.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/envSetting.h>
#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

// Extern declaration for the debug code defined in render_settings.cpp
TF_DEBUG_CODES(HDARNOLD_RENDER_SETTINGS);

namespace {

void _MsgStatusCallback(int logmask, int severity, const char* msgString, AtParamValueMap* metadata, void* userPtr)
{
    if (msgString == nullptr || userPtr == nullptr) {
        return;
    }
    static_cast<HdArnoldRenderParam*>(userPtr)->SetCachedLogMessage(msgString);
}

} // namespace

TF_DEFINE_ENV_SETTING(HDARNOLD_DEBUG_SCENE, "", "Optionally save out the arnold scene before rendering.");

HdArnoldRenderParam::~HdArnoldRenderParam()
{
    StopRenderMsgLog();
}

HdArnoldRenderParam::HdArnoldRenderParam(HdArnoldRenderDelegate* delegate) : _delegate(delegate)

{
    _needsRestart.store(false, std::memory_order::memory_order_release);
    _aborted.store(false, std::memory_order::memory_order_release);
    _paused.store(false, std::memory_order::memory_order_release);

    ResetStartTimer();

    // If the HDARNOLD_DEBUG_SCENE env variable is defined, we'll want to 
    // save out the scene every time it's about to be rendered
    _debugScene = TfGetEnvSetting(HDARNOLD_DEBUG_SCENE);
}

HdArnoldRenderParam::Status HdArnoldRenderParam::UpdateRender()
{
    // Mirror the null check Interrupt() now performs. Without it any caller
    // that ends up with a null _delegate crashes on the first GetRenderSession
    // dereference below; Aborted is a safer, observable signal that something
    // is wrong with the setup.
    if (_delegate == nullptr) {
        return Status::Aborted;
    }
    const auto aborted = _aborted.load(std::memory_order_acquire);
    // Checking early if the render was aborted earlier.
    if (aborted) {
        return Status::Aborted;
    }
    const bool needsRestart = _needsRestart.exchange(false, std::memory_order_acq_rel);
    const bool paused = _paused.exchange(false, std::memory_order_acq_rel);

    switch(AiRenderGetStatus(_delegate->GetRenderSession())) {

        case AI_RENDER_STATUS_RESTARTING:
        case AI_RENDER_STATUS_RENDERING:
            if (needsRestart) {
                _needsRestart.store(true, std::memory_order_release);
            }
            if (paused) {
                _paused.store(true, std::memory_order_release);
            }
            return Status::Converging;
        
        case AI_RENDER_STATUS_FINISHED:
            // If render restart is true, it means the Render Delegate received an update after rendering has finished
            // and AiRenderInterrupt does not change the status anymore.
            // For the atomic operations we are using a release-acquire model.
            
            if (needsRestart) {
                _paused.store(false, std::memory_order_release);
                if (!_debugScene.empty())
                    WriteDebugScene();
                AiRenderRestart(_delegate->GetRenderSession());
                RestartRenderMsgLog();
                
                ResetStartTimer();

                return Status::Converging;
            }
            StopRenderMsgLog();
            return Status::Converged;
        
        case AI_RENDER_STATUS_PAUSED:
            if (needsRestart) {
                if (!_debugScene.empty())
                    WriteDebugScene();
                AiRenderRestart(_delegate->GetRenderSession());
                RestartRenderMsgLog();
                ResetStartTimer();
            } else if (!paused) {
                AiRenderResume(_delegate->GetRenderSession());
                ResetStartTimer();
            }
            return Status::Converging;

        case AI_RENDER_STATUS_FAILED:
            // Write _errorCode BEFORE publishing _aborted with a release store.
            // The release semantics on _aborted ensure that any thread which
            // observes _aborted == true via acquire load also sees the writes
            // performed before the release — including the new _errorCode.
            _errorCode = AiRenderEnd(_delegate->GetRenderSession());
            _aborted.store(true, std::memory_order_release);
            if (_errorCode == AI_ABORT) {
                TF_WARN("[arnold-usd] Render was aborted.");
            } else if (_errorCode == AI_ERROR_NO_CAMERA) {
                TF_WARN("[arnold-usd] Camera not defined.");
            } else if (_errorCode == AI_ERROR_BAD_CAMERA) {
                TF_WARN("[arnold-usd] Bad camera data.");
            } else if (_errorCode == AI_ERROR_VALIDATION) {
                TF_WARN("[arnold-usd] Usage not validated.");
            } else if (_errorCode == AI_ERROR_RENDER_REGION) {
                TF_WARN("[arnold-usd] Invalid render region.");
            } else if (_errorCode == AI_INTERRUPT) {
                TF_WARN("[arnold-usd] Render interrupted by user.");
            } else if (_errorCode == AI_ERROR_NO_OUTPUTS) {
                TF_WARN("[arnold-usd] No rendering outputs.");
#if ARNOLD_VERSION_NUM < 70400
            } else if (_errorCode == AI_ERROR_UNAVAILABLE_DEVICE) {
                TF_WARN("[arnold-usd] Cannot create GPU context.");
#endif
            } else if (_errorCode == AI_ERROR) {
                TF_WARN("[arnold-usd] Generic error.");
            }
            StopRenderMsgLog();
            return Status::Aborted;
        
        case AI_RENDER_STATUS_NOT_STARTED:
            // If the caller already requested a pause before the render even
            // begins (e.g., a viewport that opens in a paused state), honour
            // it: do not call AiRenderBegin, and put `_paused` back so the
            // next UpdateRender iterations continue to see the pause request
            // until Resume() / Restart() clears it.
            if (paused) {
                _paused.store(true, std::memory_order_release);
                return Status::Converging;
            }
            if (!_debugScene.empty())
                WriteDebugScene();
            AiRenderBegin(_delegate->GetRenderSession());
            ResetStartTimer();
            StartRenderMsgLog();
            return Status::Converging;

        default:
            break;
    }
    return Status::Converging;
}

void HdArnoldRenderParam::Interrupt(bool needsRestart, bool clearStatus)
{
    if (_delegate == nullptr || _delegate->IsBatchContext()) return;
    const auto status = AiRenderGetStatus(_delegate->GetRenderSession());
    if (status == AI_RENDER_STATUS_RENDERING ||
        status == AI_RENDER_STATUS_RESTARTING ||
        status == AI_RENDER_STATUS_PAUSED) {
        AiRenderInterrupt(_delegate->GetRenderSession(), AI_BLOCKING);
    }
    if (needsRestart) {
        _needsRestart.store(true, std::memory_order_release);
    }
    if (clearStatus) {
        _aborted.store(false, std::memory_order_release);
    }
}

void HdArnoldRenderParam::Pause()
{
    // Publish the pause intent before issuing the interrupt. Interrupt blocks
    // until Arnold transitions the render status away from RENDERING; if the
    // store happened *after* that transition, a concurrent UpdateRender call
    // could see status == PAUSED with `_paused == false` and immediately take
    // the `!paused` branch, calling AiRenderResume and silently undoing the
    // pause that was just requested.
    _paused.store(true, std::memory_order_release);
    Interrupt(false, false);
}

void HdArnoldRenderParam::Resume() { _paused.store(false, std::memory_order_release); }

void HdArnoldRenderParam::Restart()
{
    _aborted.store(false, std::memory_order_release);
    _paused.store(false, std::memory_order_release);
    _needsRestart.store(true, std::memory_order_release);
}

bool HdArnoldRenderParam::UpdateShutter(const GfVec2f& shutter)
{
    if (!GfIsClose(_shutter[0], shutter[0], AI_EPSILON) || !GfIsClose(_shutter[1], shutter[1], AI_EPSILON)) {
        _shutter = shutter;
        return true;
    }
    return false;
}

bool HdArnoldRenderParam::UpdateFPS(const float FPS)
{
    if (!GfIsClose(_fps, FPS, AI_EPSILON)) {
        _fps = FPS;
        return true;
    }
    return false;
}

void HdArnoldRenderParam::WriteDebugScene() const
{
    if (_debugScene.empty())
        return;

    AiMsgWarning("Saving debug arnold scene as \"%s\"", _debugScene.c_str());
    AtParamValueMap* params = AiParamValueMap();
    const AtString filename(_debugScene.c_str());
    bool ok = false;
    if (params == nullptr) {
        ok = AiSceneWrite(_delegate->GetUniverse(), filename, nullptr);
    } else {
        AiParamValueMapSetBool(params, str::binary, false);
        ok = AiSceneWrite(_delegate->GetUniverse(), filename, params);
        AiParamValueMapDestroy(params);
    }
    if (!ok) {
        AiMsgWarning("Failed to save debug arnold scene as \"%s\"", _debugScene.c_str());
    }
}

double HdArnoldRenderParam::GetElapsedRenderTime() const
{
    std::chrono::time_point<std::chrono::system_clock> t0;
    {
        std::lock_guard<std::mutex> guard(_renderTimeMutex);
        t0 = _renderStartTime;
    }
    const auto t1 = std::chrono::system_clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);

    return delta.count();
}

void HdArnoldRenderParam::StartRenderMsgLog()
{

    // The "Status" logs mask was introduced in Arnold 7.1.3.0
#if ARNOLD_VERSION_NUM >= 70103
    // Deregister any previously registered callback before installing a new
    // one. Otherwise repeated Start/Start sequences (without an intervening
    // Stop) would overwrite `_msgLogCallback`, leaking the previous handle:
    // the callback function would remain registered with Arnold but we'd no
    // longer hold a handle to deregister it.
    if (_msgLogCallback >= 0) {
        AiMsgDeregisterCallback(static_cast<unsigned int>(_msgLogCallback));
        _msgLogCallback = -1;
    }
    _msgLogCallback = static_cast<int>(AiMsgRegisterCallback(_MsgStatusCallback, AI_LOG_STATUS, this));
#endif
}

void HdArnoldRenderParam::StopRenderMsgLog()
{
    if (_msgLogCallback >=0) {
        AiMsgDeregisterCallback(static_cast<unsigned int>(_msgLogCallback));
        _msgLogCallback = -1;
    }
}

void HdArnoldRenderParam::RestartRenderMsgLog()
{
    StopRenderMsgLog();
    StartRenderMsgLog();
}

std::string HdArnoldRenderParam::GetRenderStatusString() const
{
    // Use unique_lock + try_to_lock so the mutex is always released — including
    // when the std::string copy below throws (e.g., bad_alloc). The previous
    // manual lock/unlock would deadlock all future callers if the copy threw.
    std::unique_lock<std::mutex> lock(_cachedLogMutex, std::try_to_lock);
    if (lock.owns_lock()) {
        return _cachedLogMsg;
    }
    return "";
}

void HdArnoldRenderParam::SetCachedLogMessage(const char* msg)
{
    if (msg == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> guard(_cachedLogMutex);
    _cachedLogMsg = msg;
}

void HdArnoldRenderParam::SetHydraRenderSettingsPrimPath(SdfPath const &path)
{
    if (path != _hydraRenderSettingsPrimPath) {
        _hydraRenderSettingsPrimPath = path;
        TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
            "Hydra render settings prim is %s\n", path.GetText());
    }
}

const SdfPath& HdArnoldRenderParam::GetHydraRenderSettingsPrimPath() const
{
    return _hydraRenderSettingsPrimPath;
}

PXR_NAMESPACE_CLOSE_SCOPE
