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
/// @file gaussian_splat.h
///
/// Utility class to support ParticleField3DGaussianSplat primitives in Hydra.
#pragma once

#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/rprim.h>

#include "render_delegate.h"
#include "rprim.h"

PXR_NAMESPACE_OPEN_SCOPE

#if PXR_VERSION >= 2603

/// Utility class to handle ParticleField3DGaussianSplat primitives by
/// converting them to Arnold points nodes with mode=gaussian.
class HdArnoldGaussianSplat : public HdArnoldRprim<HdRprim> {
public:
    /// Constructor for HdArnoldGaussianSplat.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the primitive.
    HDARNOLD_API
    HdArnoldGaussianSplat(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

    /// Destructor for HdArnoldGaussianSplat.
    ~HdArnoldGaussianSplat() = default;

    /// Returns the initial Dirty Bits for the Primitive.
    ///
    /// @return Initial Dirty Bits.
    HDARNOLD_API
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Syncs the Hydra GaussianSplat prim to the Arnold points node.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderParam Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    /// @param reprToken Token describing the representation.
    HDARNOLD_API
    void Sync(
        HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
        const TfToken& reprToken) override;

    TfTokenVector const& GetBuiltinPrimvarNames() const override
    {
        static const TfTokenVector builtins;
        return builtins;
    }

private:
    HdArnoldPrimvarMap _primvars; ///< Precomputed list of primvars.
};

#endif // PXR_VERSION >= 2603

PXR_NAMESPACE_CLOSE_SCOPE
