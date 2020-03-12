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
}

bool HdArnoldRenderParam::Render()
{
    const auto status = AiRenderGetStatus();
    if (status == AI_RENDER_STATUS_FINISHED) {
        // If render restart is true, it means the Render Delegate received an update after rendering has finished
        // and AiRenderInterrupt does not change the status anymore.
        // For the atomic operations we are using a release-acquire model.
        const auto needsRestart = _needsRestart.exchange(false, std::memory_order_acq_rel);
        if (needsRestart) {
            AiRenderRestart();
            return false;
        }
        return true;
    }
    // Resetting the value.
    _needsRestart.store(false, std::memory_order_release);
    if (status == AI_RENDER_STATUS_PAUSED) {
        AiRenderRestart();
        return false;
    }

    if (status == AI_RENDER_STATUS_RESTARTING) {
        return false;
    }
    AiRenderBegin();
    return false;
}

void HdArnoldRenderParam::Interrupt()
{
    const auto status = AiRenderGetStatus();
    if (status != AI_RENDER_STATUS_NOT_STARTED) {
        AiRenderInterrupt(AI_BLOCKING);
        _needsRestart.store(true, std::memory_order_release);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
