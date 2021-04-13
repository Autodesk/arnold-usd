// Copyright 2021 Autodesk, Inc.
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
#include "material_tracker.h"

#include "render_delegate.h"

PXR_NAMESPACE_OPEN_SCOPE

VtArray<SdfPath> HdArnoldMaterialTracker::GetCurrentMaterials(size_t newArraySize)
{
    auto currentMaterials = _materials;
    if (_materials.size() != newArraySize) {
        _materials.resize(newArraySize);
    }
    return currentMaterials;
}

void HdArnoldMaterialTracker::SetMaterial(const SdfPath& id, size_t arrayId)
{
    if (Ai_likely(_materials.size() > arrayId)) {
        // cdata is a simple way to access the data without triggering a copy.
        if (id != _materials.cdata()[arrayId]) {
            // Trigger detaching.
            _materials[arrayId] = id;
        }
    }
}

void HdArnoldMaterialTracker::TrackMaterialChanges(
    HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId, const VtArray<SdfPath>& oldMaterials)
{
    if (!oldMaterials.IsIdentical(_materials)) {
        // All the VtArrays are shared, so we don't have to worry about duplicating data here.
        // Untracking the old materials.
        if (!oldMaterials.empty()) {
            renderDelegate->UntrackShapeMaterials(shapeId, oldMaterials);
        }
        // Tracking the new materials.
        renderDelegate->TrackShapeMaterials(shapeId, _materials);
    }
}

void HdArnoldMaterialTracker::TrackSingleMaterial(
    HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId, const SdfPath& materialId)
{
    // Initial assignment.
    if (_materials.empty()) {
        _materials.assign(1, materialId);
        renderDelegate->TrackShapeMaterials(shapeId, _materials);
        // We already have a single material stored, check if it has changed.
    } else {
        if (_materials.cdata()[0] != materialId) {
            renderDelegate->UntrackShapeMaterials(shapeId, _materials);
            _materials[0] = materialId;
            renderDelegate->TrackShapeMaterials(shapeId, _materials);
        }
    }
}

void HdArnoldMaterialTracker::UntrackMaterials(HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId)
{
    if (!_materials.empty()) {
        renderDelegate->UntrackShapeMaterials(shapeId, _materials);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
