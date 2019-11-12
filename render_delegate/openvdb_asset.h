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
/// @file openvdb_asset.h
///
/// Utilities for translating Hydra Openvdb Assets for the Render Delegate.
/// TODO:
///  * Investigate what happens when the connection between the Hydra Volume
///    and the Hydra OpenVDB Asset is broken.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/imaging/hd/field.h>

#include "render_delegate.h"

#include <mutex>
#include <unordered_set>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for translating Hydra Openvdb Asset to Arnold Volume.
class HdArnoldOpenvdbAsset : public HdField {
public:
    /// Constructor for HdArnoldOpenvdbAsset
    ///
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the OpenVDB Asset.
    HDARNOLD_API
    HdArnoldOpenvdbAsset(HdArnoldRenderDelegate* delegate, const SdfPath& id);

    /// Syncing the Hydra Openvdb Asset to the Arnold Volume.
    ///
    /// The functions main purpose is to dirty every Volume primitive's
    /// topology, so the grid definitions on the volume can be rebuilt, since
    /// changing the the grid name on the openvdb asset doesn't dirty the
    /// volume primitive, which holds the arnold volume shape.
    ///
    /// @param sceneDelegate Pointer to the Hydra Scene Delegate.
    /// @param renderParam Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    HDARNOLD_API
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;

    /// Returns the initial Dirty Bits for the Primitive.
    ///
    /// @return Initial Dirty Bits.
    HDARNOLD_API
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Tracks a HdArnoldVolume primitive.
    ///
    /// Hydra separates the volume definitions from the grids each volume
    /// requires, so we need to make sure each grid definition, which can be
    /// shared between multiple volumes, knows which volume it belongs to.
    ///
    /// @param id Path to the Hydra Volume.
    HDARNOLD_API
    void TrackVolumePrimitive(const SdfPath& id);

private:
    std::mutex _volumeListMutex; ///< Lock for the _volumeList.
    /// Storing all the Hydra Volumes using this asset.
    std::unordered_set<SdfPath, SdfPath::Hash> _volumeList;
};

PXR_NAMESPACE_CLOSE_SCOPE
