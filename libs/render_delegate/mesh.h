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
/// @file mesh.h
///
/// Utilities for translating Hydra Meshes for the Render Delegate.
#pragma once

#include "api.h"

#include <ai.h>
#include <mutex>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/mesh.h>

#include "hdarnold.h"
#include "render_delegate.h"
#include "rprim.h"
#include "utils.h"
#include "shared_arrays.h"
#include <shape_utils.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for translating Hydra Mesh to Arnold Polymesh.
class HdArnoldMesh : public HdArnoldRprim<HdMesh> {
public:
    /// Constructor for HdArnoldMesh.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the mesh.
    HDARNOLD_API
    HdArnoldMesh(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

    /// Destructor for HdArnoldMesh.
    ///
    /// Destory all Arnold Polymeshes and Ginstances.
    ~HdArnoldMesh();

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
    HDARNOLD_API
    AtNode *_GetMeshLight(HdSceneDelegate* sceneDelegate, const SdfPath& id);

    /// Returns true if this mesh carries an Arnold mesh light (which excludes it from
    /// geometry deduplication). Unlike _GetMeshLight this has no side effects.
    bool _HasMeshLight(HdSceneDelegate* sceneDelegate, const SdfPath& id) const;

    /// Computes a hash uniquely identifying the geometry that ends up on the Arnold
    /// polymesh: the mesh-specific part (topology, the display-style refinement, the subdiv
    /// tags and the resolved displacement shader - a ginstance cannot override displacement,
    /// so meshes with different displacement must not be merged) plus the part common to
    /// every geometry type (points, primvars, render tag, light linking - see
    /// HdArnoldRprim::_HashCommonGeometryState).
    ///
    /// When @p instanced is true (a point-instancer prototype), the prototype's own
    /// transform and its resolved surface shader are also folded in: the shared canonical
    /// polymesh carries both (its instancer references it directly), so only prototypes
    /// that match on those too may be merged.
    uint64_t _ComputeGeometryHash(
        const HdMeshTopology& topology, const HdArnoldSampledPrimvarType& points, HdSceneDelegate* sceneDelegate,
        const SdfPath& id, bool instanced);

    HdArnoldPrimvarMap _primvars;     ///< Precomputed list of primvars.
    HdArnoldSubsets _subsets;         ///< Material ids from subsets.
    VtValue _vertexCountsVtValue;      ///< Vertex nsides. We need to keep it alive for left handed geometries.
    bool _isLeftHanded = false;       ///< Whether the geometry is left handed or not.
    bool _useSubdiv = false;       ///< Whether the geometry use subdivision.
    size_t _vertexCountSum = 0;       ///< Sum of the vertex counts array.
    size_t _numberOfPositionKeys = 1; ///< Number of vertex position keys for the mesh.
    MeshHoleFilter _holeFilter;       ///< Cached membership/offset tables for USD holeIndices filtering.
    AtNode *_geometryLight = nullptr; ///< Eventual mesh light for this polymesh
    // Geometry deduplication state (_isInstance, _dedupRegistered, _canonicalPath, ...) lives
    // in the HdArnoldRprim base, shared with the curves rprim.
    ArrayHandler _arrayHandler; ///< Structure managing the Vt and At arrays of the scene
};

PXR_NAMESPACE_CLOSE_SCOPE
