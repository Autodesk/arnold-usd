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

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

HdArnoldRenderParam::HdArnoldRenderParam()
{
    _needsRestart.store(false, std::memory_order::memory_order_release);
    _aborted.store(false, std::memory_order::memory_order_release);
}

HdArnoldRenderParam::Status HdArnoldRenderParam::Render()
{
    const auto aborted = _aborted.load(std::memory_order_acquire);
    // Checking early if the render was aborted earlier.
    if (aborted) {
        return Status::Aborted;
    }

    const auto status = AiRenderGetStatus();
    if (status == AI_RENDER_STATUS_FINISHED) {
        // If render restart is true, it means the Render Delegate received an update after rendering has finished
        // and AiRenderInterrupt does not change the status anymore.
        // For the atomic operations we are using a release-acquire model.
        const auto needsRestart = _needsRestart.exchange(false, std::memory_order_acq_rel);
        if (needsRestart) {
            _paused.store(false, std::memory_order_release);
            AiRenderRestart();
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
            AiRenderRestart();
        } else if (!_paused.load(std::memory_order_acquire)) {
            AiRenderResume();
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
        const auto errorCode = AiRenderEnd();
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
    AiRenderBegin();
    return Status::Converging;
}

void HdArnoldRenderParam::Interrupt(bool needsRestart, bool clearStatus)
{
    const auto status = AiRenderGetStatus();
    if (status != AI_RENDER_STATUS_NOT_STARTED) {
        AiRenderInterrupt(AI_BLOCKING);
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

PXR_NAMESPACE_CLOSE_SCOPE
