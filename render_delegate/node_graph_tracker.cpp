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
#include "node_graph_tracker.h"

#include "render_delegate.h"

PXR_NAMESPACE_OPEN_SCOPE

VtArray<SdfPath> HdArnoldNodeGraphTracker::GetCurrentNodeGraphs(size_t newArraySize)
{
    auto currentNodeGraphs = _nodeGraphs;
    if (_nodeGraphs.size() != newArraySize) {
        _nodeGraphs.resize(newArraySize);
    }
    return currentNodeGraphs;
}

void HdArnoldNodeGraphTracker::SetNodeGraph(const SdfPath& id, size_t arrayId)
{
    if (Ai_likely(_nodeGraphs.size() > arrayId)) {
        // cdata is a simple way to access the data without triggering a copy.
        if (id != _nodeGraphs.cdata()[arrayId]) {
            // Trigger detaching.
            _nodeGraphs[arrayId] = id;
        }
    }
}

void HdArnoldNodeGraphTracker::TrackNodeGraphChanges(
    HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId, const VtArray<SdfPath>& oldNodeGraphs)
{
    if (!oldNodeGraphs.IsIdentical(_nodeGraphs)) {
        // All the VtArrays are shared, so we don't have to worry about duplicating data here.
        // Untracking the old materials.
        if (!oldNodeGraphs.empty()) {
            renderDelegate->UntrackShapeNodeGraphs(shapeId, oldNodeGraphs);
        }
        // Tracking the new materials.
        renderDelegate->TrackShapeNodeGraphs(shapeId, _nodeGraphs);
    }
}

void HdArnoldNodeGraphTracker::TrackSingleNodeGraph(
    HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId, const SdfPath& nodeGraphId)
{
    // Initial assignment.
    if (_nodeGraphs.empty()) {
        _nodeGraphs.assign(1, nodeGraphId);
        renderDelegate->TrackShapeNodeGraphs(shapeId, _nodeGraphs);
        // We already have a single material stored, check if it has changed.
    } else {
        if (_nodeGraphs.cdata()[0] != nodeGraphId) {
            renderDelegate->UntrackShapeNodeGraphs(shapeId, _nodeGraphs);
            _nodeGraphs[0] = nodeGraphId;
            renderDelegate->TrackShapeNodeGraphs(shapeId, _nodeGraphs);
        }
    }
}

void HdArnoldNodeGraphTracker::UntrackNodeGraphs(HdArnoldRenderDelegate* renderDelegate, const SdfPath& shapeId)
{
    if (!_nodeGraphs.empty()) {
        renderDelegate->UntrackShapeNodeGraphs(shapeId, _nodeGraphs);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
