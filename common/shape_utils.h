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
/// @file shape_utils.h
///
/// Shared utils for shapes.
#include <pxr/pxr.h>

#include <pxr/base/vt/array.h>

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Read subdivision creases from a Usd or a Hydra mesh.
///
/// @param node Arnold node to set the creases on.
/// @param cornerIndices Indices of the corners.
/// @param cornerWeights Weights of the corners
/// @param creaseIndices Indices of creases.
/// @param creaseLengths Length of each crease.
/// @param creaseWeights Weight of each crease.
void ArnoldUsdReadCreases(
    AtNode* node, const VtIntArray& cornerIndices, const VtFloatArray& cornerWeights, const VtIntArray& creaseIndices,
    const VtIntArray& creaseLengths, const VtFloatArray& creaseWeights);

PXR_NAMESPACE_CLOSE_SCOPE
