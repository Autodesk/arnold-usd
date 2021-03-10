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

PXR_NAMESPACE_CLOSE_SCOPE
