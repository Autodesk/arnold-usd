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
#include "rprim.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for translating Hydra Mesh to Arnold Polymesh.
class HdArnoldMesh : public HdArnoldRprim<HdMesh> {
public:
#if PXR_VERSION >= 2102
    /// Constructor for HdArnoldMesh.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the mesh.
    HDARNOLD_API
    HdArnoldMesh(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);
#else
    /// Constructor for HdArnoldMesh.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the mesh.
    /// @param instancerId Path to the Point Instancer for this mesh.
    HDARNOLD_API
    HdArnoldMesh(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const SdfPath& instancerId = SdfPath());
#endif

    /// Destructor for HdArnoldMesh.
    ///
    /// Destory all Arnold Polymeshes and Ginstances.
    ~HdArnoldMesh() override = default;

    /// Syncs the Hydra Mesh to the Arnold Polymesh.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderParam Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    /// @param reprToken Token describing the representation of the mesh.
    HDARNOLD_API
    void Sync(
        HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
        const TfToken& reprToken) override;

    /// Returns the initial Dirty Bits for the Primitive.
    ///
    /// @return Initial Dirty Bits.
    HDARNOLD_API
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    /// Returns true if step size is bigger than zero, false otherwise.
    ///
    /// @return True if polymesh is a volume boundary.
    HDARNOLD_API
    bool _IsVolume() const;

    HDARNOLD_API
    AtNode *_GetMeshLight(HdSceneDelegate* sceneDelegate, const SdfPath& id);

    HdArnoldPrimvarMap _primvars;     ///< Precomputed list of primvars.
    HdArnoldSubsets _subsets;         ///< Material ids from subsets.
    VtIntArray _vertexCounts;         ///< Vertex Counts array for reversing vertex and primvar polygon order.
    size_t _vertexCountSum = 0;       ///< Sum of the vertex counts array.
    size_t _numberOfPositionKeys = 1; ///< Number of vertex position keys for the mesh.
    AtNode *_geometryLight = nullptr; ///< Eventual mesh light for this polymesh
};

PXR_NAMESPACE_CLOSE_SCOPE
