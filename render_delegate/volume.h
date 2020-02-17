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
/// @file volume.h
///
/// Utilities for handling Hydra Volumes.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/imaging/hd/volume.h>

#include "hdarnold.h"
#include "render_delegate.h"
#include "shape.h"

#include <ai.h>

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for Hydra Volumes.
class HdArnoldVolume : public HdVolume {
public:
    /// Constructor for HdArnoldVolume.
    ///
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the Primitive.
    /// @param instancerId Path to the Point Instancer.
    HDARNOLD_API
    HdArnoldVolume(HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId = SdfPath());

    /// Destructor for HdArnoldVolume.
    ///
    /// Frees all the Arnold Volume created.
    HDARNOLD_API
    ~HdArnoldVolume() override;

    /// Syncs the Hydra Volume to the Arnold Volume.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderParam Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    /// @param reprToken Token describing the representation of the volume.
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

    /// Initialize a given representation for the volume.
    ///
    /// @param reprName Name of the representation to initialize.
    /// @param dirtyBits In/Out HdDirtyBits value, that allows the _InitRepr
    ///  function to set additional Dirty Bits if required for a given
    ///  representation.
    HDARNOLD_API
    void _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override;

    /// Creates the volumes for the primitive.
    ///
    /// In Hydra each volume is connected to several grid/field primitives
    /// where each grid/field has a name and file path associated. In Arnold
    /// each Volume loads one or more grids from a file, so in this function
    /// we check each grid/field connected, and create multiple Volume
    /// primitives for each file loaded.
    ///
    /// @param id Path to the Primitive.
    /// @param delegate Pointer to the Scene Delegate.
    HDARNOLD_API
    void _CreateVolumes(const SdfPath& id, HdSceneDelegate* delegate);

    /// Iterates through all available volumes and calls a function on each of them.
    ///
    /// @tparam F Generic type for the function.
    /// @param f Function to run on the volumes.
    template <typename F>
    void _ForEachVolume(F&& f)
    {
        for (auto* v : _volumes) {
            f(v);
        }
        for (auto* v : _inMemoryVolumes) {
            f(v);
        }
    }

    HdArnoldRenderDelegate* _delegate;            ///< Pointer to the Render Delegate.
    std::vector<HdArnoldShape*> _volumes;         ///< Vector storing all the Volumes created.
    std::vector<HdArnoldShape*> _inMemoryVolumes; ///< Vectoring storing all the Volumes for in-memory VDB storage.
};

PXR_NAMESPACE_CLOSE_SCOPE
