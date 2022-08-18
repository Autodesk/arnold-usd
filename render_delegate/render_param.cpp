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
#include "render_param.h"
#include "render_delegate.h"
#include <constant_strings.h>

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
HdArnoldRenderParam::HdArnoldRenderParam(HdArnoldRenderDelegate* delegate) : _delegate(delegate)
#else
HdArnoldRenderParam::HdArnoldRenderParam()
#endif
{
    _needsRestart.store(false, std::memory_order::memory_order_release);
    _aborted.store(false, std::memory_order::memory_order_release);
    // If the HDARNOLD_DEBUG_SCENE env variable is defined, we'll want to 
    // save out the scene every time it's about to be rendered
    const char *HDARNOLD_DEBUG_SCENE = std::getenv("HDARNOLD_DEBUG_SCENE");
    if (HDARNOLD_DEBUG_SCENE) 
        _debugScene = std::string(HDARNOLD_DEBUG_SCENE);
}

HdArnoldRenderParam::Status HdArnoldRenderParam::Render()
{
    const auto aborted = _aborted.load(std::memory_order_acquire);
    // Checking early if the render was aborted earlier.
    if (aborted) {
        return Status::Aborted;
    }

#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    const auto status = AiRenderGetStatus(_delegate->GetRenderSession());
#else
    const auto status = AiRenderGetStatus();
#endif
    if (status == AI_RENDER_STATUS_FINISHED) {
        // If render restart is true, it means the Render Delegate received an update after rendering has finished
        // and AiRenderInterrupt does not change the status anymore.
        // For the atomic operations we are using a release-acquire model.
        const auto needsRestart = _needsRestart.exchange(false, std::memory_order_acq_rel);
        if (needsRestart) {
            _paused.store(false, std::memory_order_release);
            if (!_debugScene.empty())
                WriteDebugScene();
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
            AiRenderRestart(_delegate->GetRenderSession());
#else
            AiRenderRestart();
#endif
            return Status::Converging;
        }
        return Status::Converged;
    }
    // Resetting the value.
    _needsRestart.store(false, std::memory_order_release);
    if (status == AI_RENDER_STATUS_PAUSED) {
        const auto needsRestart = _needsRestart.exchange(false, std::memory_order_acq_rel);
        if (needsRestart) {
            _paused.store(false, std::memory_order_release);
            if (!_debugScene.empty())
                WriteDebugScene();
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
            AiRenderRestart(_delegate->GetRenderSession());
#else
            AiRenderRestart();
#endif
        } else if (!_paused.load(std::memory_order_acquire)) {
            if (!_debugScene.empty())
                WriteDebugScene();
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
            AiRenderResume(_delegate->GetRenderSession());
#else
            AiRenderResume();
#endif
        }
        return Status::Converging;
    }

    if (status == AI_RENDER_STATUS_RESTARTING) {
        _paused.store(false, std::memory_order_release);
        return Status::Converging;
    }

    if (status == AI_RENDER_STATUS_FAILED) {
        _aborted.store(true, std::memory_order_release);
        _paused.store(false, std::memory_order_release);
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        const auto errorCode = AiRenderEnd(_delegate->GetRenderSession());
#else
        const auto errorCode = AiRenderEnd();
#endif
        if (errorCode == AI_ABORT) {
            TF_WARN("[arnold-usd] Render was aborted.");
        } else if (errorCode == AI_ERROR_NO_CAMERA) {
            TF_WARN("[arnold-usd] Camera not defined.");
        } else if (errorCode == AI_ERROR_BAD_CAMERA) {
            TF_WARN("[arnold-usd] Bad camera data.");
        } else if (errorCode == AI_ERROR_VALIDATION) {
            TF_WARN("[arnold-usd] Usage not validated.");
        } else if (errorCode == AI_ERROR_RENDER_REGION) {
            TF_WARN("[arnold-usd] Invalid render region.");
        } else if (errorCode == AI_INTERRUPT) {
            TF_WARN("[arnold-usd] Render interrupted by user.");
        } else if (errorCode == AI_ERROR_NO_OUTPUTS) {
            TF_WARN("[arnold-usd] No rendering outputs.");
        } else if (errorCode == AI_ERROR_UNAVAILABLE_DEVICE) {
            TF_WARN("[arnold-usd] Cannot create GPU context.");
        } else if (errorCode == AI_ERROR) {
            TF_WARN("[arnold-usd] Generic error.");
        }
        return Status::Aborted;
    }
    _paused.store(false, std::memory_order_release);
    if (status != AI_RENDER_STATUS_RENDERING) {
        if (!_debugScene.empty())
            WriteDebugScene();
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        AiRenderBegin(_delegate->GetRenderSession());
#else
        AiRenderBegin();
#endif
    }
    return Status::Converging;
}

void HdArnoldRenderParam::Interrupt(bool needsRestart, bool clearStatus)
{
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    const auto status = AiRenderGetStatus(_delegate->GetRenderSession());
#else
    const auto status = AiRenderGetStatus();
#endif
    if (status != AI_RENDER_STATUS_NOT_STARTED) {
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
        AiRenderInterrupt(_delegate->GetRenderSession(), AI_BLOCKING);
#else
        AiRenderInterrupt(AI_BLOCKING);
#endif
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
    Interrupt(false, false);
    _paused.store(true, std::memory_order_release);
}

void HdArnoldRenderParam::Resume() { _paused.store(false, std::memory_order_release); }

void HdArnoldRenderParam::Restart()
{
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
    AiParamValueMapSetBool(params, str::binary, false);
    AiSceneWrite(_delegate->GetUniverse(), AtString(_debugScene.c_str()), params);
    AiParamValueMapDestroy(params);
}

PXR_NAMESPACE_CLOSE_SCOPE
