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
#include "shape_utils.h"

#include "constant_strings.h"

PXR_NAMESPACE_OPEN_SCOPE

void ArnoldUsdReadCreases(
    AtNode* node, const VtIntArray& cornerIndices, const VtFloatArray& cornerWeights, const VtIntArray& creaseIndices,
    const VtIntArray& creaseLengths, const VtFloatArray& creaseWeights)
{
    // Hydra/USD has two types of subdiv tags, corners anc creases. Arnold supports both, but corners
    // are emulated by duplicating the indices of the corner and treating it like a crease.
    const auto cornerIndicesCount = static_cast<uint32_t>(cornerIndices.size());
    // Number of crease segment that we'll need to generate the arnold weights.
    uint32_t creaseSegmentCount = 0;
    for (auto creaseLength : creaseLengths) {
        // The number of segments will always be one less than the number of points defining the edge.
        creaseSegmentCount += std::max(0, creaseLength - 1);
    }

    const auto creaseIdxsCount = cornerIndicesCount * 2 + creaseSegmentCount * 2;
    const auto creaseSharpnessCount = cornerIndicesCount + creaseSegmentCount;

    auto* creaseIdxsArray = AiArrayAllocate(creaseIdxsCount, 1, AI_TYPE_UINT);
    auto* creaseSharpnessArray = AiArrayAllocate(creaseSharpnessCount, 1, AI_TYPE_FLOAT);

    auto* creaseIdxs = static_cast<uint32_t*>(AiArrayMap(creaseIdxsArray));
    auto* creaseSharpness = static_cast<float*>(AiArrayMap(creaseSharpnessArray));

    // Corners are creases with duplicated indices.
    uint32_t ii = 0;
    for (auto cornerIndex : cornerIndices) {
        creaseIdxs[ii * 2] = cornerIndex;
        creaseIdxs[ii * 2 + 1] = cornerIndex;
        creaseSharpness[ii] = cornerWeights[ii];
        ++ii;
    }

    // Indexing into the crease indices array.
    uint32_t jj = 0;
    // Indexing into the crease weights array.
    uint32_t ll = 0;
    for (auto creaseLength : creaseLengths) {
        for (auto k = decltype(creaseLength){1}; k < creaseLength; ++k, ++ii) {
            creaseIdxs[ii * 2] = creaseIndices[jj + k - 1];
            creaseIdxs[ii * 2 + 1] = creaseIndices[jj + k];
            creaseSharpness[ii] = creaseWeights[ll];
        }
        jj += creaseLength;
        ll += 1;
    }

    AiNodeSetArray(node, str::crease_idxs, creaseIdxsArray);
    AiNodeSetArray(node, str::crease_sharpness, creaseSharpnessArray);
}



ArnoldUsdCurvesData::ArnoldUsdCurvesData(int vmin, int vstep, const VtIntArray &vertexCounts) :
         _vmin(vmin),
         _vstep(vstep),
         _vertexCounts(vertexCounts),
         _numPerVertex(0)
    {
    }

 // We are pre-calculating the per vertex counts for the Arnold curves object, which is different
// from USD's.
// Arnold only supports per segment user data, so we need to precalculate.
// Arnold always requires segment + 1 number of user data per each curve.
// For linear curves, the number of user data is always the same as the number of vertices.
// For non-linear curves, we can use vstep and vmin to calculate it.

void ArnoldUsdCurvesData::InitVertexCounts()
{
    if (!_arnoldVertexCounts.empty())
        return;
    
    const auto numVertexCounts = _vertexCounts.size();
    _arnoldVertexCounts.resize(numVertexCounts);
    for (auto i = decltype(numVertexCounts){0}; i < numVertexCounts; i += 1) {
        const auto numSegments = (_vertexCounts[i] - _vmin) / _vstep + 1;
        _arnoldVertexCounts[i] = numSegments + 1;
        _numPerVertex += numSegments + 1;
    }   
}

/// Sets radius attribute on an Arnold shape from a VtValue holding VtFloatArray. We expect this to be a width value,
/// so a (*0.5) function will be applied to the values.
///
/// @param node Pointer to an Arnold node.
/// @param value Value holding a VtFloatfArray.

void ArnoldUsdCurvesData::SetRadiusFromValue(AtNode *node, const VtValue& value)
{
    AtArray* arr = nullptr;
    if (value.IsHolding<VtFloatArray>()) {
        const auto& values = value.UncheckedGet<VtFloatArray>();
        arr = AiArrayAllocate(values.size(), 1, AI_TYPE_FLOAT);
        auto* out = static_cast<float*>(AiArrayMap(arr));
        std::transform(values.begin(), values.end(), out, [](const float w) -> float { return w * 0.5f; });
        AiArrayUnmap(arr);
    } else if (value.IsHolding<VtDoubleArray>()) {
        const auto& values = value.UncheckedGet<VtDoubleArray>();
        arr = AiArrayAllocate(values.size(), 1, AI_TYPE_FLOAT);
        auto* out = static_cast<float*>(AiArrayMap(arr));
        std::transform(
            values.begin(), values.end(), out, [](const double w) -> float { return static_cast<float>(w * 0.5); });
        AiArrayUnmap(arr);
    } else if (value.IsHolding<float>()) {
        arr = AiArray(1, 1, AI_TYPE_FLOAT, value.UncheckedGet<float>() / 2.0f);
    } else if (value.IsHolding<double>()) {
        arr = AiArray(1, 1, AI_TYPE_FLOAT, static_cast<float>(value.UncheckedGet<double>() / 2.0));
    } else {
        return;
    }

    AiNodeSetArray(node, str::radius, arr);
}

PXR_NAMESPACE_CLOSE_SCOPE
