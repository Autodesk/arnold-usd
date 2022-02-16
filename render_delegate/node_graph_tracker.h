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
class HdArnoldNodeGraphTracker {
public:
    /// Queries the list of current materials.
    ///
    /// @param newArraySize Size of the materials after querying the old array.
    /// @return A copy of the current materials.
    HDARNOLD_API
    VtArray<SdfPath> GetCurrentNodeGraphs(size_t newArraySize);

    /// Check if a material has changed and store the new material.
    ///
    /// @param id Path to the new material.
    /// @param arrayId Index of the material.
    HDARNOLD_API
    void SetNodeGraph(const SdfPath& id, size_t arrayId);

    /// Track material changes if materials has been changed.
    ///
    /// @param renderDelegate Pointer to the Arnold Render Delegate.
    /// @param shapeId Id of the current shape.
    /// @param oldMaterials List of the materials assigned to the shape before assigning materials.
    HDARNOLD_API
    void TrackNodeGraphChanges(
        HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId, const VtArray<SdfPath>& oldNodeGraphs);

    /// Track node graph if there is only a single node graph assigned to the shape.
    ///
    /// @param renderDelegate Pointer to the Arnold Render Delegate.
    /// @param shapeId Id of the current shape.
    /// @param nodeGraphId The material assigned to the shape.
    HDARNOLD_API
    void TrackSingleNodeGraph(
        HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId, const SdfPath& nodeGraphId);

    /// Untrack all materials assigned to the shape. Typically used when deleting the shape.
    ///
    /// @param renderDelegate Pointer to the Arnold Render Delegate.
    /// @param shapeId Id of the current shape.
    HDARNOLD_API
    void UntrackNodeGraphs(HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId);

private:
    VtArray<SdfPath> _nodeGraphs; ///< List of materials currently assigned.
};

PXR_NAMESPACE_CLOSE_SCOPE
