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
#include "openvdb_asset.h"

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

HdArnoldOpenvdbAsset::HdArnoldOpenvdbAsset(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id) : HdField(id)
{
    TF_UNUSED(renderDelegate);
}

void HdArnoldOpenvdbAsset::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    TF_UNUSED(renderParam);
    if (*dirtyBits & HdField::DirtyParams) {
        auto& changeTracker = sceneDelegate->GetRenderIndex().GetChangeTracker();
        // But accessing this list happens on a single thread,
        // as bprims are synced before rprims.
        for (const auto& volume : _volumeList) {
            changeTracker.MarkRprimDirty(volume, HdChangeTracker::DirtyTopology);
        }
    }
    *dirtyBits = HdField::Clean;
}

HdDirtyBits HdArnoldOpenvdbAsset::GetInitialDirtyBitsMask() const { return HdField::AllDirty; }

// This will be called from multiple threads.
void HdArnoldOpenvdbAsset::TrackVolumePrimitive(const SdfPath& id)
{
    std::lock_guard<std::mutex> lock(_volumeListMutex);
    _volumeList.insert(id);
}

PXR_NAMESPACE_CLOSE_SCOPE
