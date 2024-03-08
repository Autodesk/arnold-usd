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
/// @file render_delegate/node_graph.h
///
/// Utilities for translating Hydra Materials and Node Graphs for the Render Delegate.
#pragma once

#include <pxr/pxr.h>
#include <pxr/imaging/hd/material.h>

#include "api.h"
#include <constant_strings.h>
#include "render_delegate.h"
#include <ai.h>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for translating Hydra Node Graphs to Arnold nodes.
class HdArnoldOptions : public HdSprim {
public:
    enum DirtyBits : HdDirtyBits
    {
        Clean                 = 0,
        DirtyParams           = 1 << 0
    };


    HDARNOLD_API
    HdArnoldOptions(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

    ~HdArnoldOptions() override;

    /// Syncing the Hydra Material to the Arnold Shader Network.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderPaaram Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    HDARNOLD_API
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;

    /// Returns the initial Dirty Bits for the Primitive.
    HDARNOLD_API
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:

    HdArnoldRenderDelegate* _renderDelegate; ///< Pointer to the Render Delegate.
};

PXR_NAMESPACE_CLOSE_SCOPE
