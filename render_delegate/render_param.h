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
    HDARNOLD_API
    void Interrupt();

    /// Clear aborted status of the render, so it can be resumed by calling Render again.
    HDARNOLD_API
    void ClearStatus();

private:
    /// Indicate if render needs restarting, in case interrupt is called after rendering has finished.
    std::atomic<bool> _needsRestart;
    /// Indicate if rendering has been aborted at one point or another.
    bool _aborted = false;
};

PXR_NAMESPACE_CLOSE_SCOPE
