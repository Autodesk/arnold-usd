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
/// @file node_graph_tracker.h
///
/// Utilities for tracking node_graph changes on shapes.
#pragma once

#include "api.h"

#include <pxr/pxr.h>

#include <pxr/base/vt/array.h>

#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldRenderDelegate;

/// Class to track material assignments to shapes.
class HdArnoldMaterialTracker {
public:
    /// Queries the list of current materials.
    ///
    /// @param newArraySize Size of the materials after querying the old array.
    /// @return A copy of the current materials.
    HDARNOLD_API
    VtArray<SdfPath> GetCurrentMaterials(size_t newArraySize);

    /// Check if a material has changed and store the new material.
    ///
    /// @param id Path to the new material.
    /// @param arrayId Index of the material.
    HDARNOLD_API
    void SetMaterial(const SdfPath& id, size_t arrayId);

    /// Track material changes if materials has been changed.
    ///
    /// @param renderDelegate Pointer to the Arnold Render Delegate.
    /// @param shapeId Id of the current shape.
    /// @param oldMaterials List of the materials assigned to the shape before assigning materials.
    HDARNOLD_API
    void TrackMaterialChanges(
        HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId, const VtArray<SdfPath>& oldMaterials);

    /// Track material if there is only a single material assigned to the shape.
    ///
    /// @param renderDelegate Pointer to the Arnold Render Delegate.
    /// @param shapeId Id of the current shape.
    /// @param materialId The material assigned to the shape.
    HDARNOLD_API
    void TrackSingleMaterial(HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId, const SdfPath& materialId);

    /// Untrack all materials assigned to the shape. Typically used when deleting the shape.
    ///
    /// @param renderDelegate Pointer to the Arnold Render Delegate.
    /// @param shapeId Id of the current shape.
    HDARNOLD_API
    void UntrackMaterials(HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId);

private:
    VtArray<SdfPath> _materials; ///< List of materials currently assigned.
};

PXR_NAMESPACE_CLOSE_SCOPE
