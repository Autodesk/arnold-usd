// Copyright 2019 Autodesk, Inc.
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
/// @file points.h
///
/// Utility class to support point primitives in Hydra.
#pragma once

#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/points.h>

#include "render_delegate.h"
#include "shape.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class to handle point primitives.
class HdArnoldPoints : public HdPoints {
public:
    /// Constructor for HdArnoldPoints.
    ///
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the points.
    /// @param instancerId Path to the Point Instancer for this points.
    HDARNOLD_API
    HdArnoldPoints(HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId = SdfPath());

    /// Destructor for HdArnoldPoints.
    ///
    /// Destory all Arnold Points and Ginstances.
    HDARNOLD_API
    ~HdArnoldPoints();

    /// Returns the initial Dirty Bits for the Primitive.
    ///
    /// @return Initial Dirty Bits.
    HDARNOLD_API
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Syncs the Hydra Points to the Arnold Points.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderPaaram Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    /// @param reprToken Token describing the representation of the points.
    HDARNOLD_API
    void Sync(HdSceneDelegate* delegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
        override;

protected:
    /// Allows setting additional Dirty Bits based on the ones already set.
    ///
    /// @param bits The current Dirty Bits.
    /// @return The new set of Dirty Bits which replace the original one.
    HDARNOLD_API
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

    /// Initialize a given representation for the points.
    ///
    /// @param reprName Name of the representation to initialize.
    /// @param dirtyBits In/Out HdDirtyBits value, that allows the _InitRepr
    ///  function to set additional Dirty Bits if required for a given
    ///  representation.
    HDARNOLD_API
    void _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override;

    HdArnoldShape _shape; ///< Utility class for the points and the instances.
};

PXR_NAMESPACE_CLOSE_SCOPE
