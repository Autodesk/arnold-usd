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
/// @file nodes.h
///
/// Interfaces for Arnold nodes used by the Render Delegate.
#pragma once

#include <pxr/pxr.h>

#include <pxr/base/gf/matrix4f.h>

#include <pxr/base/tf/token.h>

#include "../render_buffer.h"

#include <ai.h>

#include <functional>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

struct DriverMainData {
    GfMatrix4f projMtx = GfMatrix4f{1.0f};
    GfMatrix4f viewMtx = GfMatrix4f{1.0f};
    HdArnoldRenderBuffer* colorBuffer = nullptr;
    HdArnoldRenderBuffer* depthBuffer = nullptr;
    HdArnoldRenderBuffer* idBuffer = nullptr;
    // Local storage for converting from P to depth.
    std::vector<float> depths[AI_MAX_THREADS];
    // Local storage for the id remapping.
    std::vector<int> ids[AI_MAX_THREADS];
    // Local storage for the color buffer.
    std::vector<AtRGBA> colors[AI_MAX_THREADS];

    // Map of buffers per AOV name
    std::unordered_map<AtString, HdArnoldRenderBuffer*, AtStringHash> buffers;

    // Store the region Min so that we apply an offset when negative 
    // pixel coordinates are needed for overscan
    int regionMinX = 0;
    int regionMinY = 0;
};

/// Installs Arnold nodes that are used by the Render Delegate.
void hdArnoldInstallNodes();

PXR_NAMESPACE_CLOSE_SCOPE
