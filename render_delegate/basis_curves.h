// Copyright 2020 Autodesk, Inc.
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

#include "shape.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldBasisCurves : public HdBasisCurves {
public:
    HDARNOLD_API
    HdArnoldBasisCurves(HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId = SdfPath());

    /// Syncs the Hydra Basis Curves to the Arnold Curves.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderPaaram Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    /// @param reprToken Token describing the representation of the mesh.
    void Sync(HdSceneDelegate* delegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
        override;

    /// Returns the initial Dirty Bits for the Primitive.
    ///
    /// @return Initial Dirty Bits.
    HdDirtyBits GetInitialDirtyBitsMask() const override;

protected:
    /// Allows setting additional Dirty Bits based on the ones already set.
    ///
    /// @param bits The current Dirty Bits.
    /// @return The new set of Dirty Bits which replace the original one.
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

    /// Initialize a given representation for the curves.
    ///
    /// @param reprName Name of the representation to initialize.
    /// @param dirtyBits In/Out HdDirtyBits value, that allows the _InitRepr
    ///  function to set additional Dirty Bits if required for a given
    ///  representation.
    void _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override;

    HdArnoldShape _shape;         ///< Utility class for the curves and instances.
    HdArnoldPrimvarMap _primvars; ///< Precomputed list of primvars.
    TfToken _interpolation;       ///< Interpolation of the curve.
    VtIntArray _vertexCounts;     ///< Stored vertex counts for curves.
};

PXR_NAMESPACE_CLOSE_SCOPE
