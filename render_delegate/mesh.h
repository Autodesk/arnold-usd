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
/// @file mesh.h
///
/// Utilities for translating Hydra Meshes for the Render Delegate.
#pragma once

#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/mesh.h>

#include "hdarnold.h"
#include "render_delegate.h"
#include "shape.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for translating Hydra Mesh to Arnold Polymesh.
class HdArnoldMesh : public HdMesh {
public:
    /// Constructor for HdArnoldMesh.
    ///
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the mesh.
    /// @param instancerId Path to the Point Instancer for this mesh.
    HDARNOLD_API
    HdArnoldMesh(HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId = SdfPath());

    /// Destructor for HdArnoldMesh.
    ///
    /// Destory all Arnold Polymeshes and Ginstances.
    ~HdArnoldMesh() override = default;

    /// Syncs the Hydra Mesh to the Arnold Polymesh.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderPaaram Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    /// @param reprToken Token describing the representation of the mesh.
    HDARNOLD_API
    void Sync(HdSceneDelegate* delegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
        override;

    /// Returns the initial Dirty Bits for the Primitive.
    ///
    /// @return Initial Dirty Bits.
    HDARNOLD_API
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    /// Allows setting additional Dirty Bits based on the ones already set.
    ///
    /// @param bits The current Dirty Bits.
    /// @return The new set of Dirty Bits which replace the original one.
    HDARNOLD_API
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

    /// Initialize a given representation for the mesh.
    ///
    /// @param reprName Name of the representation to initialize.
    /// @param dirtyBits In/Out HdDirtyBits value, that allows the _InitRepr
    ///  function to set additional Dirty Bits if required for a given
    ///  representation.
    HDARNOLD_API
    void _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override;

    /// Returns true if step size is bigger than zero, false otherwise.
    ///
    /// @return True if polymesh is a volume boundary.
    HDARNOLD_API
    bool _IsVolume() const;

    HdArnoldShape _shape;             ///< Utility class for the mesh and instances.
    HdArnoldPrimvarMap _primvars;     ///< Precomputed list of primvars.
    VtIntArray _vertexCounts;         ///< Vertex Counts array for reversing vertex and primvar polygon order.
    size_t _numberOfPositionKeys = 1; ///< Number of vertex position keys for the mesh.
};

PXR_NAMESPACE_CLOSE_SCOPE
