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

// How long an imager-only refresh is considered to be still in flight after it was requested, see
// IsImagerUpdateInFlight(). Imagers are a post-process over the image already rendered and normally land
// within a couple of milliseconds; the window only has to be wide enough for the host to tick us at least
// once more, so the refreshed buffers get read and presented.
constexpr auto k_imager_update_window = std::chrono::milliseconds(250);

int64_t NowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
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
    _stopped.store(false, std::memory_order::memory_order_release);

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
    // An imager graph Sync parks imager evaluation and HasPendingChanges() lifts it once the graph's
    // connections have been applied - which is always before this point in a frame. Finding it still
    // parked here means that frame took a path that never got there, so lift it rather than leave
    // imager evaluation gated for the rest of the render.
    if (_imagersInterrupted.load(std::memory_order_acquire)) {
        ResumeImagers();
    }

    const auto aborted = _aborted.load(std::memory_order_acquire);
    // Checking early if the render was aborted earlier.
    if (aborted) {
        return Status::Aborted;
    }
    const bool needsRestart = _needsRestart.exchange(false, std::memory_order_acq_rel);
    // _paused and _stopped are plain state, not one-shot intents: read them without consuming them, so there is no
    // window where they read false while the render is genuinely paused/stopped, and no branch below has to
    // remember to store them back. Only Pause()/Stop() set them and only Resume()/Restart()/Interrupt() clear them.
    const bool paused = _paused.load(std::memory_order_acquire);
    const bool stopped = _stopped.load(std::memory_order_acquire);

    const auto renderStatus = AiRenderGetStatus(_delegate->GetRenderSession());
    switch(renderStatus) {

        case AI_RENDER_STATUS_RESTARTING:
        case AI_RENDER_STATUS_RENDERING:
            if (needsRestart) {
                _needsRestart.store(true, std::memory_order_release);
            }
#if ARNOLD_VERSION_NUM >= 70504
            // A gated render keeps reporting AI_RENDER_STATUS_RENDERING, so the status alone cannot tell us whether
            // the pause request already took effect: ask Arnold directly. AiRenderPause() only takes effect while
            // the status is exactly RENDERING and silently no-ops otherwise, so this is where a Pause() that raced
            // a RESTARTING window, or one issued while the session was sitting interrupt-PAUSED, actually gets
            // established -- and doing it here rather than unconditionally avoids re-issuing AiRenderPause() on
            // every single tick of the poll loop for the whole duration of the pause.
            if (paused && renderStatus == AI_RENDER_STATUS_RENDERING &&
                !AiRenderIsPaused(_delegate->GetRenderSession())) {
                AiRenderPause(_delegate->GetRenderSession());
            }
#endif
            return Status::Converging;

        case AI_RENDER_STATUS_FINISHED:
            // If render restart is true, it means the Render Delegate received an update after rendering has finished
            // and AiRenderInterrupt does not change the status anymore.
            // For the atomic operations we are using a release-acquire model.

            if (stopped) {
                // Stop() is in effect: keep the restart request queued for the Restart() that lifts it, rather
                // than relaunching a render the host asked us to stop.
                if (needsRestart) {
                    _needsRestart.store(true, std::memory_order_release);
                }
                return Status::Converging;
            }
            if (needsRestart) {
                // Restarting from FINISHED is a real restart, exactly like the pause-cancelling Interrupt() that
                // usually precedes it, so the pause is no longer in effect -- clear it instead of leaving a stale
                // flag behind (there is nothing left to unpause: the gated threads are long gone).
                _paused.store(false, std::memory_order_release);
                if (!_debugScene.empty())
                    WriteDebugScene();
                AiRenderRestart(_delegate->GetRenderSession());
                RestartRenderMsgLog();
                
                ResetStartTimer();

                return Status::Converging;
            }
            // An imager-only refresh (see HdArnoldNodeGraph::Sync on an imager graph) deliberately does
            // not restart the render, so the status stays FINISHED throughout. Keep reporting Converging
            // until the refresh has had a chance to land in the render buffers, otherwise the host stops
            // reading them and never presents the new image.
            if (IsImagerUpdateInFlight()) {
                return Status::Converging;
            }
            StopRenderMsgLog();
            return Status::Converged;
        
        case AI_RENDER_STATUS_PAUSED:
            // Arnold reports PAUSED after an AiRenderInterrupt(), i.e. the render threads are parked and progress
            // has been discarded. Left to itself this branch resumes the render on the very next tick, which is
            // what a scene edit wants -- but not what Stop() wants, so honour a stop before anything else and keep
            // any pending restart queued for the Restart() that lifts it.
            if (stopped) {
                if (needsRestart) {
                    _needsRestart.store(true, std::memory_order_release);
                }
                return Status::Converging;
            }
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
            // If the caller already requested a pause or a stop before the render even begins (e.g., a viewport
            // that opens in a paused state), honour it: do not call AiRenderBegin. Arnold cannot help here --
            // AiRenderPause() silently no-ops on a session that has not started -- so this relies entirely on
            // `_paused`/`_stopped`, which stay set until Resume() / Restart() clears them. Keep any pending restart
            // request queued for then; it is inert before the render begins (AiRenderBegin picks up every edit
            // anyway), but dropping it would lose the signal for anything else that polls it.
            if (paused || stopped) {
                if (needsRestart) {
                    _needsRestart.store(true, std::memory_order_release);
                }
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

void HdArnoldRenderParam::InterruptImagers()
{
    // Same exclusions as Interrupt(): in a batch render nothing is being displayed as it is computed, and
    // under a procedural parent we do not own the render session driving the imagers.
    if (_delegate == nullptr || _delegate->IsBatchContext() || _delegate->GetProceduralParent() != nullptr)
        return;
#if ARNOLD_VERSION_NUM >= 70504
    // Every imager graph Sync in a frame asks for this, but a single ResumeImagers() lifts it, so only
    // park once.
    if (_imagersInterrupted.exchange(true, std::memory_order_acq_rel))
        return;
    AtRenderSession* session = _delegate->GetRenderSession();
    // AiImagerInterrupt() blocks until no imager evaluation is reading the imager shaders. There cannot be
    // one before AiRenderBegin(), and calling into a session that has not started has nothing to park.
    if (AiRenderGetStatus(session) != AI_RENDER_STATUS_NOT_STARTED)
        AiImagerInterrupt(session);
#endif
}

void HdArnoldRenderParam::ResumeImagers()
{
    if (_delegate == nullptr || _delegate->IsBatchContext() || _delegate->GetProceduralParent() != nullptr)
        return;
    AtRenderSession* session = _delegate->GetRenderSession();
#if ARNOLD_VERSION_NUM >= 70504
    _imagersInterrupted.store(false, std::memory_order_release);
    // Resuming is equivalent to setting the request_imager_update hint, there is no need to do both.
    AiImagerResume(session);
#else
    AiRenderSetHintBool(session, str::request_imager_update, true);
#endif
    _imagerUpdateTime.store(NowNs(), std::memory_order_release);
}

bool HdArnoldRenderParam::IsImagerUpdateInFlight() const
{
    const int64_t requested = _imagerUpdateTime.load(std::memory_order_acquire);
    if (requested == 0)
        return false;
    return std::chrono::nanoseconds(NowNs() - requested) < k_imager_update_window;
}

void HdArnoldRenderParam::Interrupt(bool needsRestart, bool clearStatus, bool clearPaused)
{
    // Skip in batch renders, and when we run under a procedural parent: in that
    // case we don't own the render loop and must not interrupt the host's render
    // session (which would otherwise happen now that the procedural reports its
    // real, interactive session type).
    if (_delegate == nullptr || _delegate->IsBatchContext() || _delegate->GetProceduralParent() != nullptr) return;
    const auto status = AiRenderGetStatus(_delegate->GetRenderSession());
    if (status == AI_RENDER_STATUS_RENDERING ||
        status == AI_RENDER_STATUS_RESTARTING ||
        status == AI_RENDER_STATUS_PAUSED) {
        AiRenderInterrupt(_delegate->GetRenderSession(), AI_BLOCKING);
        if (clearPaused) {
            // A real interrupt unparks any in-flight render, including one gated by AiRenderPause(): it clears
            // Arnold's own pause flag (AiRenderIsPaused() goes false) and parks the session at
            // AI_RENDER_STATUS_PAUSED, from where UpdateRender() resumes or restarts it. Clear _paused to match, so
            // it doesn't keep claiming "paused" once the render is active again -- otherwise it gets stuck true
            // forever, since nothing else clears it on this path.
            _paused.store(false, std::memory_order_release);
        }
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
#if ARNOLD_VERSION_NUM >= 70504
    // Arnold 7.5.4+ can pause render threads at a gate without terminating
    // them, preserving render progress, instead of the old interrupt-based
    // approach below which fully tears down the in-flight render.
    // AiRenderPause() is issued unconditionally rather than guarded on the render status like Interrupt() is: it
    // only takes effect while the status is exactly RENDERING and silently no-ops in every other state, and the
    // `_paused` store below is what makes the request stick until UpdateRender() can establish the gate.
    _paused.store(true, std::memory_order_release);
    if (_delegate != nullptr && !_delegate->IsBatchContext() && _delegate->GetProceduralParent() == nullptr) {
        AiRenderPause(_delegate->GetRenderSession());
    }
#else
    // Publish the pause intent before issuing the interrupt. Interrupt blocks
    // until Arnold transitions the render status away from RENDERING; if the
    // store happened *after* that transition, a concurrent UpdateRender call
    // could see status == PAUSED with `_paused == false` and immediately take
    // the `!paused` branch, calling AiRenderResume and silently undoing the
    // pause that was just requested.
    // Pass clearPaused=false: here the interrupt itself is the mechanism used to
    // reach the paused state, not an unrelated scene edit, so it must not undo the
    // `_paused` store above.
    _paused.store(true, std::memory_order_release);
    Interrupt(false, false, false);
#endif
}

void HdArnoldRenderParam::Resume()
{
    const bool wasPaused = _paused.exchange(false, std::memory_order_acq_rel);
    const bool wasStopped = _stopped.exchange(false, std::memory_order_acq_rel);
#if ARNOLD_VERSION_NUM >= 70504
    // Explicitly lift the pause gate here rather than waiting for the next
    // UpdateRender poll, since AiRenderGetStatus() keeps reporting RENDERING
    // while paused and UpdateRender has no other signal to act on.
    if (_delegate != nullptr && !_delegate->IsBatchContext() && _delegate->GetProceduralParent() == nullptr) {
        AiRenderResume(_delegate->GetRenderSession());
    }
#endif
    if (wasPaused || wasStopped) {
        // Restart the clock so the time spent parked at the gate is not billed to the render: the elapsed time is
        // reported as render statistics, and UpdateRender()'s own resume path resets it for the same reason.
        ResetStartTimer();
    }
}

void HdArnoldRenderParam::Stop()
{
    // A hard stop: interrupt the render for real (discarding progress) rather than parking it at the resumable
    // AiRenderPause() gate, and latch `_stopped` so UpdateRender() keeps it stopped. Without that latch the
    // interrupt only leaves the session at AI_RENDER_STATUS_PAUSED, which the very next UpdateRender() tick
    // resumes -- so the render would restart on its own, one frame after being stopped.
    // Publish the stop before issuing the interrupt, for the same reason Pause() does: Interrupt() blocks until
    // Arnold transitions the render status away from RENDERING, so a store afterwards leaves a window where a
    // concurrent UpdateRender call sees status == PAUSED with `_stopped == false` and resumes the render we were
    // asked to stop. Note this also latches when there is no render to interrupt, which is intended: a stop
    // requested before AiRenderBegin() has to keep UpdateRender() from starting one.
    _stopped.store(true, std::memory_order_release);
    Interrupt(false, false);
}

void HdArnoldRenderParam::Restart()
{
    _aborted.store(false, std::memory_order_release);
    _paused.store(false, std::memory_order_release);
    _stopped.store(false, std::memory_order_release);
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

// The interrupt/resume pair itself lives on HdArnoldRenderParam: a deferred resume (DeferResume)
// outlives this object, and the refresh it requests has to be visible to UpdateRender(), which
// decides whether the render still counts as converging.
HdArnoldRenderParam* HdArnoldImagerInterrupt::_GetRenderParam() const
{
    if (_delegate == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<HdArnoldRenderParam*>(_delegate->GetRenderParam());
}

void HdArnoldImagerInterrupt::Interrupt()
{
    if (_hasInterrupted) {
        return;
    }
    // Nothing to bracket in a batch render or under a procedural parent, matching
    // HdArnoldRenderParam::Interrupt(): there we don't own the render loop and the host brackets its
    // own edits. Under a procedural parent it would also deadlock, since this runs from inside the
    // render's scene update, which Arnold counts as an imager reader - so it would wait on itself.
    HdArnoldRenderParam* param = _GetRenderParam();
    if (param == nullptr || _delegate->IsBatchContext() || _delegate->GetProceduralParent() != nullptr) {
        return;
    }
    _hasInterrupted = true;
    // Blocks until no thread is inside an imager evaluation, so the imager nodes and the shading
    // trees they read can be edited safely.
    param->InterruptImagers();
}

void HdArnoldImagerInterrupt::Resume()
{
    if (!_hasInterrupted) {
        return;
    }
    _hasInterrupted = false;
    if (HdArnoldRenderParam* param = _GetRenderParam()) {
        param->ResumeImagers();
    }
}

void HdArnoldImagerInterrupt::DeferResume()
{
    if (!_hasInterrupted) {
        return;
    }
    // Disarm this instance: the delegate owes the resume from here on, and issues it from
    // HasPendingChanges() once the queued connections have been applied. HdArnoldRenderParam
    // keeps the barrier's state, so UpdateRender() can lift it as a last resort if a frame ever
    // takes a path that never gets there.
    _hasInterrupted = false;
    if (_delegate != nullptr) {
        _delegate->RequestImagerUpdate();
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
