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
/// @file shape_utils.h
///
/// Shared utils for shapes.
#pragma once
#include <pxr/pxr.h>

#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>

#include <pxr/base/arch/export.h>

#include "common_utils.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Read subdivision creases from a Usd or a Hydra mesh.
///
/// @param node Arnold node to set the creases on.
/// @param cornerIndices Indices of the corners.
/// @param cornerWeights Weights of the corners
/// @param creaseIndices Indices of creases.
/// @param creaseLengths Length of each crease.
/// @param creaseWeights Weight of each crease.
ARCH_HIDDEN
void ArnoldUsdReadCreases(
    AtNode* node, const VtIntArray& cornerIndices, const VtFloatArray& cornerWeights, const VtIntArray& creaseIndices,
    const VtIntArray& creaseLengths, const VtFloatArray& creaseWeights);

/// Helper class meant to read primvars & width from a USD / Hydra curves
/// It will eventually handle remapping of vertex & varying primvars,
/// since this is not supported in Arnold. To do so, it might initialize
/// a remapping table on demand, so that the same data can be reused
/// for multiple primvars.
class ArnoldUsdCurvesData {
public:
    /// Constructor for ArnoldUsdCurvesData.
    ///
    /// @param vmin Minimum number of vertices per segment.
    /// @param vstep Number of vertices needed to increase segment count by one.
    /// @param vertexCounts Original vertex counts from USD.
    ArnoldUsdCurvesData(int vmin, int vstep, const VtIntArray& vertexCounts);
    /// Default destructor for ArnoldUsdCurvesData.
    ~ArnoldUsdCurvesData() = default;

    /// Initialize Arnold Vertex Counts using vmin/vstep and the USD vertex counts.
    void InitVertexCounts();
    /// Set the Arnold curves radius from a VtValue.
    ///
    /// @param node Arnold node to set the radius on.
    /// @param value VtValue holding the radius values.
    static void SetRadiusFromValue(AtNode* node, const VtValue& value);

    /// Set the Arnold curves orientation from a VtValue.
    ///
    /// @param node Arnold node to set the orientations on.
    /// @param value VtValue holding the orientation values.
    void SetOrientationFromValue(AtNode* node, const VtValue& value);

    template <typename T>
    inline bool RemapCurvesVertexPrimvar(VtValue& value)
    {
        if (!value.IsHolding<VtArray<T>>()) {
            return false;
        }
        InitVertexCounts();
        const auto numVertexCounts = _arnoldVertexCounts.size();

        if (Ai_unlikely(_vertexCounts.size() != numVertexCounts)) {
            return true;
        }

        const auto& original = value.UncheckedGet<VtArray<T>>();
        if (_numPerVertex == static_cast<int>(original.size())) {
            // The input value size already matches what we're targetting. There's no
            // need to do any remapping
            return true;
        }
        VtArray<T> remapped(_numPerVertex);
        const auto* originalP = original.data();
        auto* remappedP = remapped.data();
        // We use the first and the last item for each curve and using the CanInterpolate type.
        // - Interpolate values if we can interpolate the type.
        // - Look for the closest one if we can't interpolate the type.
        for (auto curve = decltype(numVertexCounts){0}; curve < numVertexCounts; curve += 1) {
            const auto originalVertexCount = _vertexCounts[curve];
            const auto arnoldVertexCount = _arnoldVertexCounts[curve];
            const auto arnoldVertexCountMinusOne = arnoldVertexCount - 1;
            const auto originalVertexCountMinusOne = originalVertexCount - 1;
            *remappedP = *originalP;
            remappedP[arnoldVertexCountMinusOne] = originalP[originalVertexCountMinusOne];

            // The original vertex count should always be more than the
            if (arnoldVertexCount > 2) {
                for (auto i = 1; i < arnoldVertexCountMinusOne; i += 1) {
                    // Convert i to a range of 0..1.
                    const auto arnoldVertex = static_cast<float>(i) / static_cast<float>(arnoldVertexCountMinusOne);
                    const auto originalVertex = arnoldVertex * static_cast<float>(originalVertexCountMinusOne);
                    // AiLerp fails with string and other types, so we have to make sure it's not being called based
                    // on the type, so using a partial template specialization, that does not work with functions.
                    RemapVertexPrimvar<T>::fn(remappedP[i], originalP, originalVertex);
                }
            }
            originalP += originalVertexCount;
            remappedP += arnoldVertexCount;
        }

        // This is the one we are supposed to use when it's expensive to copy objects to VtValue and we don't care
        // about the object taken anymore.
        value = VtValue::Take(remapped);
        return true;
    }

    /// Remapping vertex primvar from USD to Arnold.
    ///
    /// @tparam T0 T1 T Variadic template parameters holding every type we check for.
    /// @param value VtValue holding the USD vertex primvar, if remapping is successful, @param value will be changed.
    /// @return True if remapping was successful, false otherwise.
    template <typename T0, typename T1, typename... T>
    inline bool RemapCurvesVertexPrimvar(VtValue& value)
    {
        return RemapCurvesVertexPrimvar<T0>(value) || RemapCurvesVertexPrimvar<T1, T...>(value);
    }

private:
    VtIntArray _arnoldVertexCounts;  ///< Arnold vertex counts.
    const VtIntArray& _vertexCounts; ///< USD vertex counts.
    int _vmin;                       ///< Minimum vertex count per segment.
    int _vstep;                      ///< Number of vertices needed to increase segment count by one.
    int _numPerVertex;               ///< Number of per vertex values.
    int _numPoints;                  ///< Total amount of vertices.

    template <typename T0, typename... T>
    struct IsAny : std::false_type {
    };

    template <typename T0, typename T1>
    struct IsAny<T0, T1> : std::is_same<T0, T1> {
    };

    template <typename T0, typename T1, typename... T>
    struct IsAny<T0, T1, T...> : std::integral_constant<bool, std::is_same<T0, T1>::value || IsAny<T0, T...>::value> {
    };

    template <typename T>
    using CanInterpolate = IsAny<T, float, double, GfHalf, GfVec2f, GfVec3f, GfVec4f>;

    template <typename T, bool interpolate = CanInterpolate<T>::value>
    struct RemapVertexPrimvar {
        static inline void fn(T&, const T*, float) {}
    };

    template <typename T>
    struct RemapVertexPrimvar<T, false> {
        static inline void fn(T& remapped, const T* original, float originalVertex)
        {
            remapped = original[static_cast<int>(floorf(originalVertex))];
        }
    };

    template <typename T>
    struct RemapVertexPrimvar<T, true> {
        static inline void fn(T& remapped, const T* original, float originalVertex)
        {
            float originalVertexFloor = 0;
            const auto originalVertexFrac = modff(originalVertex, &originalVertexFloor);
            const auto originalVertexFloorInt = static_cast<int>(originalVertexFloor);
            remapped =
                AiLerp(originalVertexFrac, original[originalVertexFloorInt], original[originalVertexFloorInt + 1]);
        }
    };
};

ARCH_HIDDEN
bool FlattenIndexedValue(const VtValue& in, const VtIntArray& idx, VtValue& out);
/// Function to query if an arnold: prefixed parameter can be ignored on an Arnold schema.
///
/// @param name Name of the parameter (including the "arnold:" prefix).
/// @return True if the parameter can be ignored, false otherwise.
ARCH_HIDDEN
bool ArnoldUsdIgnoreUsdParameter(const TfToken& name);

/// Function to query if an arnold parameter can be ignored on an Arnold schema.
///
/// @param name Name of the parameter (NOT including the "arnold:" prefix).
/// @return True if the parameter can be ignored, false otherwise.
ARCH_HIDDEN
bool ArnoldUsdIgnoreParameter(const AtString& name);


/// Generates the idxs array for flattened USD values. When @p vertexCounts is not nullptr and not empty, the
/// the indices are reversed per polygon. The sum of the values stored in @p vertexCounts is expected to match
/// @p numIdxs if @p vertexCountSum is not provided.
///
/// @param numIdxs Number of face vertex indices to generate.
/// @param vertexCounts Optional VtArrayInt pointer to the face vertex counts of the mesh or nullptr.
/// @param vertexCountSum Optional size_t with sum of the vertexCounts.
/// @return An AtArray with the generated indices of @param numIdxs length.
AtArray* GenerateVertexIdxs(
    unsigned int numIdxs, const VtIntArray* vertexCounts = nullptr, const size_t* vertexCountSum = nullptr);
/// Generate the idxs array for indexed primvars. When @p vertexCounts is non-nullptor, it's going to be used
/// to flip orientation of polygons.
///
/// @param indices Face-varying indices from Hydra.
/// @param vertexCounts Optional vertex counts of the polymesh, which will be used to flip polygon orientation if no
/// @return An AtArray converted from @p indices containing face-varying indices.
AtArray* GenerateVertexIdxs(const VtIntArray& indices, const VtIntArray* vertexCounts = nullptr);

/// Generate the idxs array for indexed primvars with vertex interpolation.
///
/// @param indices Vertex indices from Hydra for this primvar.
/// @param vidxs  Vertex indices array from Arnold (vidxs)
/// @return An AtArray converted from @p indices containing face-varying indices.
AtArray* GenerateVertexIdxs(const VtIntArray& indices, AtArray* vidxs);

/// Type to store arnold param names and values.
using ArnoldUsdParamValueList = std::vector<std::pair<AtString, VtValue>>;

/// Helper that filters mesh data to drop entries belonging to "hole" faces
/// (as expressed by USD's `holeIndices` attribute).
///
/// The class pre-computes:
///   - a `std::vector<bool>` membership table for O(1) hole-face tests, and
///   - a prefix sum of the original face-vertex counts so that face-varying
///     entries can be filtered with a single range copy per face.
///
/// All filter methods are no-ops (return false) when `Empty()` is true,
/// so callers can safely wrap them unconditionally.
class ARCH_HIDDEN MeshHoleFilter {
public:
    MeshHoleFilter() = default;

    /// Build / rebuild the internal lookup tables.
    /// If @p holeIndices is empty, the filter becomes empty (no-op).
    void Build(const VtIntArray& holeIndices, const VtIntArray& originalFaceVertexCounts);

    /// Reset to the empty state.
    void Clear();

    bool Empty() const { return _holeCount == 0; }
    size_t NumOriginalFaces() const { return _isHole.size(); }
    size_t NumKeptFaces() const { return _isHole.size() - _holeCount; }
    size_t NumHoles() const { return _holeCount; }
    /// True if the face index is marked as a hole. Index must be < NumOriginalFaces().
    bool IsHole(size_t face) const { return _isHole[face]; }

    /// Filter a per-face (uniform) array (one entry per face).
    /// Returns true if filtering was applied; false on no-op (filter empty
    /// or array size does not match the original face count).
    template <typename T>
    bool FilterUniformArray(VtArray<T>& arr) const
    {
        if (Empty()) return false;
        if (arr.size() != _isHole.size()) return false;
        // Read via const cdata() to avoid COW detach on a possibly-shared
        // input; the input is then replaced via move-assign below.
        const T* src = arr.cdata();
        VtArray<T> out;
        out.reserve(NumKeptFaces());
        for (size_t i = 0; i < _isHole.size(); ++i) {
            if (!_isHole[i]) out.push_back(src[i]);
        }
        arr = std::move(out);
        return true;
    }
    template <typename T>
    bool FilterUniformArray(std::vector<T>& arr) const
    {
        if (Empty()) return false;
        if (arr.size() != _isHole.size()) return false;
        std::vector<T> out;
        out.reserve(NumKeptFaces());
        for (size_t i = 0; i < _isHole.size(); ++i) {
            if (!_isHole[i]) out.push_back(arr[i]);
        }
        arr = std::move(out);
        return true;
    }

    /// Filter a face-varying array (entries grouped per face, sized to the
    /// sum of the original face vertex counts). Uses range copies based on
    /// the pre-computed prefix-sum offsets.
    template <typename T>
    bool FilterFaceVaryingArray(VtArray<T>& arr) const
    {
        if (Empty()) return false;
        const size_t expected = _offsets.empty() ? 0 : _offsets.back();
        if (arr.size() < expected) return false;
        // cdata() does not trigger COW detach on the input.
        const T* src = arr.cdata();
        VtArray<T> out;
        out.reserve(expected); // upper bound; actual is expected - holeVertexCount
        for (size_t i = 0; i < _isHole.size(); ++i) {
            if (_isHole[i]) continue;
            for (size_t j = _offsets[i]; j < _offsets[i + 1]; ++j) {
                out.push_back(src[j]);
            }
        }
        arr = std::move(out);
        return true;
    }
    template <typename T>
    bool FilterFaceVaryingArray(std::vector<T>& arr) const
    {
        if (Empty()) return false;
        const size_t expected = _offsets.empty() ? 0 : _offsets.back();
        if (arr.size() < expected) return false;
        std::vector<T> out;
        out.reserve(expected);
        for (size_t i = 0; i < _isHole.size(); ++i) {
            if (_isHole[i]) continue;
            out.insert(out.end(), arr.begin() + _offsets[i], arr.begin() + _offsets[i + 1]);
        }
        arr = std::move(out);
        return true;
    }

    /// Filter a uniform VtValue by trying the common primvar element types.
    /// Returns true if a matching type was found and filtered.
    bool FilterUniformValue(VtValue& value) const;
    /// Filter a face-varying VtValue by trying the common primvar element types.
    bool FilterFaceVaryingValue(VtValue& value) const;

private:
    std::vector<bool> _isHole;     ///< Per-face hole membership (size = numOriginalFaces).
    std::vector<size_t> _offsets;  ///< Prefix sum of face vertex counts (size = numOriginalFaces + 1).
    size_t _holeCount = 0;         ///< Number of unique hole faces.
};

PXR_NAMESPACE_CLOSE_SCOPE
