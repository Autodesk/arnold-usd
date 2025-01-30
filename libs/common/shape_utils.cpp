//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Autodesk, Inc.
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
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec2h.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/base/gf/vec4h.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix4d.h>

#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((matrix, "arnold:matrix"))
    ((disp_map, "arnold:disp_map"))
    ((visibility, "arnold:visibility"))
    ((name, "arnold:name"))
    ((shader, "arnold:shader"))
    ((id, "arnold:id"))
);
// clang-format on

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

ArnoldUsdCurvesData::ArnoldUsdCurvesData(int vmin, int vstep, const VtIntArray& vertexCounts)
    : _vertexCounts(vertexCounts), _vmin(vmin), _vstep(vstep), _numPerVertex(0), _numPoints(0)
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

    const unsigned int numVertexCounts = _vertexCounts.size();
    _arnoldVertexCounts.resize(numVertexCounts);
    for (unsigned int i = 0; i < numVertexCounts; i++) {
        const int numSegments = (_vertexCounts[i] - _vmin) / _vstep + 1;
        _arnoldVertexCounts[i] = numSegments + 1;
        _numPerVertex += numSegments + 1;
        _numPoints += _vertexCounts[i];
    }
}

/// Sets radius attribute on an Arnold shape from a VtValue holding VtFloatArray. We expect this to be a width value,
/// so a (*0.5) function will be applied to the values.
///
/// @param node Pointer to an Arnold node.
/// @param value Value holding a VtFloatfArray.

void ArnoldUsdCurvesData::SetRadiusFromValue(AtNode* node, const VtValue& value)
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
    } else if (value.IsHolding<VtHalfArray>()) {
        const auto& values = value.UncheckedGet<VtHalfArray>();
        arr = AiArrayAllocate(values.size(), 1, AI_TYPE_FLOAT);
        auto* out = static_cast<float*>(AiArrayMap(arr));
        std::transform(
            values.begin(), values.end(), out, [](const GfHalf w) -> float { return static_cast<float>(w) * 0.5f; });
        AiArrayUnmap(arr);
    } else if (value.IsHolding<float>()) {
        arr = AiArray(1, 1, AI_TYPE_FLOAT, value.UncheckedGet<float>() / 2.0f);
    } else if (value.IsHolding<double>()) {
        arr = AiArray(1, 1, AI_TYPE_FLOAT, static_cast<float>(value.UncheckedGet<double>() / 2.0));
    } else if (value.IsHolding<GfHalf>()) {
        arr = AiArray(1, 1, AI_TYPE_FLOAT, static_cast<float>(value.UncheckedGet<GfHalf>()) / 2.0f);
    } else {
        return;
    }

    AiNodeSetArray(node, str::radius, arr);
}

// Set the Arnold curves orientation from a VtValue.
void ArnoldUsdCurvesData::SetOrientationFromValue(AtNode* node, const VtValue& value)
{    
    // Only consider Vec3f arrays for now
    if (!value.IsHolding<VtVec3fArray>())
        return;

    InitVertexCounts();

    const VtVec3fArray& values = value.UncheckedGet<VtVec3fArray>();
    // Arnold requires the amount of orientation values to be the same as the amount of points
    if (values.size() == _numPoints) {
        AiNodeSetArray(node, str::orientations, AiArrayConvert(values.size(), 1, AI_TYPE_VECTOR, values.data()));
        // If orientation is set on the arnold curves, then the mode needs to be "oriented"
        AiNodeSetStr(node, str::mode, str::oriented);
    } else {
        // Ignore other use cases for now. 
        AiMsgWarning("%s : Found %zu curves normals, expected %d", AiNodeGetName(node), values.size(), _numPoints);
    }
}


bool ArnoldUsdIgnoreUsdParameter(const TfToken& name)
{
    return name == _tokens->matrix || name == _tokens->disp_map || 
           name == _tokens->name || name == _tokens->shader || name == _tokens->id;
}

bool ArnoldUsdIgnoreParameter(const AtString& name)
{
    return name == str::matrix || name == str::disp_map || name == str::visibility ||
           name == str::name || name == str::shader || name == str::id;
}
AtArray* GenerateVertexIdxs(const VtIntArray& indices, const VtIntArray* vertexCounts)
{
    const auto numIdxs = static_cast<uint32_t>(indices.size());
    if (numIdxs < 3) {
        return AiArrayAllocate(0, 1, AI_TYPE_UINT);
    }
    auto* array = AiArrayAllocate(numIdxs, 1, AI_TYPE_UINT);
    if (numIdxs > 0) {
        auto* out = static_cast<uint32_t*>(AiArrayMap(array));
        if (vertexCounts != nullptr && !vertexCounts->empty()) {
            unsigned int vertexId = 0;
            for (auto vertexCount : *vertexCounts) {
                if (Ai_unlikely(vertexCount <= 0) || Ai_unlikely(vertexId + vertexCount > numIdxs)) {
                    continue;
                }
                for (auto vertex = decltype(vertexCount){0}; vertex < vertexCount; vertex += 1) {
                    out[vertexId + vertex] = indices[vertexId + vertexCount - vertex - 1];
                }
                vertexId += vertexCount;
            }
        } else {
            std::copy(indices.begin(), indices.end(), out);
        }

        AiArrayUnmap(array);
    }
    return array;
}

AtArray* GenerateVertexIdxs(unsigned int numIdxs, const VtIntArray* vertexCounts, const size_t* vertexCountSum)
{
    if (vertexCountSum != nullptr && numIdxs != *vertexCountSum) {
        return AiArrayAllocate(0, 1, AI_TYPE_UINT);
    }
    auto* array = AiArrayAllocate(numIdxs, 1, AI_TYPE_UINT);
    if (numIdxs > 0) {
        auto* out = static_cast<uint32_t*>(AiArrayMap(array));
        // Flip indices per polygon to support left handed topologies.
        if (vertexCounts != nullptr && !vertexCounts->empty()) {
            unsigned int vertexId = 0;
            for (auto vertexCount : *vertexCounts) {
                if (Ai_unlikely(vertexCount <= 0)) {
                    continue;
                }
                for (auto vertex = decltype(vertexCount){0}; vertex < vertexCount; vertex += 1) {
                    out[vertexId + vertex] = vertexId + vertexCount - vertex - 1;
                }
                vertexId += vertexCount;
            }
        } else {
            for (auto index = decltype(numIdxs){0}; index < numIdxs; index += 1) {
                out[index] = index;
            }
        }
        AiArrayUnmap(array);
    }
    return array;
}

/* Returns the indices AtArray for a primvar with vertex interpolation.
    
  By default it returns a copy of the vertex indices (vidxs) array that was previously set
  in the Arnold mesh. 
  However, USD also supports primvars with vertex interpolations along with an indexed list,
  whereas Arnold assumes that indexed attributes are always per face-vertex. 
  When indices are present for this primvar, this function will remap them to have the same size
  as vidxs.
**/
AtArray* GenerateVertexIdxs(const VtIntArray& indices, AtArray* vidxs)
{    
    if (vidxs == nullptr || AiArrayGetNumElements(vidxs) == 0) {
        return AiArrayAllocate(0, 1, AI_TYPE_UINT);
    }
    // This primvar has no indices, so we return a copy of vidxs
    // NOTE that if vidx is a shared array, it will create a shallow copy of it and reference it internally
    // which can will potentially lead to a call on double free memory error
    if (indices.empty())
        return AiArrayCopy(vidxs);

    const auto numIdxs = static_cast<uint32_t>(AiArrayGetNumElements(vidxs));
    AtArray* array = AiArrayAllocate(numIdxs, 1, AI_TYPE_UINT);
    uint32_t* out = static_cast<uint32_t*>(AiArrayMap(array));
    const uint32_t* in = static_cast<const uint32_t*>(AiArrayMapConst(vidxs));
   
    for (unsigned int i = 0; i < numIdxs; ++i) {
        if (in[i] >= indices.size()) {
            out[i] = {};
            continue;            
        }
        out[i] = indices[in[i]];
    }

    AiArrayUnmap(array);
    AiArrayUnmapConst(vidxs);
    return array;
}

template <typename T>
inline bool _FlattenIndexedValue(const VtValue& in, const VtIntArray& idx, VtValue& out)
{
    if (!in.IsHolding<VtArray<T>>())
        return false;

    const VtArray<T>& inArray = in.UncheckedGet<VtArray<T>>();

    VtArray<T> outArray;
    outArray.resize(idx.size());

    std::vector<size_t> invalidIndexPositions;
    for (size_t i = 0; i < idx.size(); i++) {
        outArray[i] = inArray[AiClamp(idx[i], 0, int(inArray.size()) - 1)];
    }
    out = VtValue::Take(outArray);
    return true;
}

template <typename T0, typename T1, typename... T>
inline bool _FlattenIndexedValue(const VtValue& in, const VtIntArray& idx, VtValue& out)
{
    return _FlattenIndexedValue<T0>(in, idx, out) || _FlattenIndexedValue<T1, T...>(in, idx, out);
}

bool FlattenIndexedValue(const VtValue& in, const VtIntArray& idx, VtValue& out)
{
    if (!in.IsArrayValued())
        return false;
    if (idx.empty())
        return false;

    return _FlattenIndexedValue<float, double, GfVec2f, GfVec2d, GfVec3f, 
                GfVec3d, GfVec4f, GfVec4d, int, unsigned int, unsigned char, bool,
                TfToken, GfHalf, GfVec2h, GfVec3h, GfVec4h, GfMatrix4f, GfMatrix4d>(in, idx, out);

}
PXR_NAMESPACE_CLOSE_SCOPE
