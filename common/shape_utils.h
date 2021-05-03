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
#include <pxr/base/vt/value.h>

#include <pxr/base/arch/export.h>

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

/// Type to store arnold param names and values.
using ArnoldUsdParamValueList = std::vector<std::pair<AtString, VtValue>>;

PXR_NAMESPACE_CLOSE_SCOPE
