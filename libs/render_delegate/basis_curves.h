//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Autodesk, Inc.
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
/// @file basis_curves.h
///
/// Utilities for translating Hydra Basis Curves for the Render Delegate.
#pragma once

#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/basisCurves.h>

#include "rprim.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldBasisCurves : public HdArnoldRprim<HdBasisCurves> {
public:
    /// Constructor for HdArnoldBasisCurves.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the basis curves.
    HDARNOLD_API
    HdArnoldBasisCurves(HdArnoldRenderDelegate* delegate, const SdfPath& id);

    /// Destructor for HdArnoldBasisCurves.
    ///
    /// Destory all Arnold curves and ginstances.
    ~HdArnoldBasisCurves() override;

    /// Syncs the Hydra Basis Curves to the Arnold Curves.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderParam Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    /// @param reprToken Token describing the representation of the mesh.
    void Sync(
        HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
        const TfToken& reprToken) override;

    /// Returns the initial Dirty Bits for the Primitive.
    ///
    /// @return Initial Dirty Bits.
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    /// Computes a hash uniquely identifying the geometry that ends up on the Arnold curves
    /// node: the curve-specific part (vertex counts, indices, type, basis, wrap - curves have
    /// no displacement or subdivision) plus the part common to every geometry type (points,
    /// primvars, render tag, light linking - see HdArnoldRprim::_HashCommonGeometryState).
    ///
    /// When @p instanced is true (a point-instancer prototype), the prototype's own transform
    /// and its resolved surface shader are also folded in: the shared canonical curves node
    /// carries both (its instancer references it directly), so only prototypes that match on
    /// those too may be merged.
    uint64_t _ComputeGeometryHash(
        const HdBasisCurvesTopology& topology, const HdArnoldSampledPrimvarType& points,
        HdSceneDelegate* sceneDelegate, const SdfPath& id, bool instanced);

    HdArnoldPrimvarMap _primvars; ///< Precomputed list of primvars.
    TfToken _interpolation;       ///< Interpolation of the curve.
    VtIntArray _vertexCounts;     ///< Stored vertex counts for curves.
};

PXR_NAMESPACE_CLOSE_SCOPE
