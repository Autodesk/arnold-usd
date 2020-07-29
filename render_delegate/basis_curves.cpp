// Copyright 2020 Autodesk, Inc.
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
#include "basis_curves.h"

#include "constant_strings.h"
#include "material.h"
#include "utils.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>

#include <pxr/usd/sdf/assetPath.h>

/*
 * TODO:
 *  - Add support for per instance variables.
 *  - Investigate periodic and pinned curves.
 *  - Convert normals to orientations.
 *  - Allow overriding basis via a primvar and remap all the parameters.
 *  - Correctly handle degenerate curves using KtoA as an example.
 */

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (pscale)
);
// clang-format on

namespace {

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
using CanInterpolate = IsAny<T, float, double, GfVec2f, GfVec3f, GfVec4f>;

template <typename T, bool interpolate = CanInterpolate<T>::value>
struct RemapVertexPrimvar {
    static inline void fn(T&, const T*, float) { }
};

template <typename T>
struct RemapVertexPrimvar<T, false> {
    static inline void fn(T& remapped, const T* original, float originalVertex) {
        remapped = original[static_cast<int>(floorf(originalVertex))];
    }
};

template <typename T>
struct RemapVertexPrimvar<T, true> {
    static inline void fn(T& remapped, const T* original, float originalVertex) {
        float originalVertexFloor = 0;
        const auto originalVertexFrac = modf(originalVertex, &originalVertexFloor);
        const auto originalVertexFloorInt = static_cast<int>(originalVertexFloor);
        remapped = AiLerp(originalVertexFrac, original[originalVertexFloorInt], original[originalVertexFloorInt + 1]);
    }
};

template <typename T>
inline bool _RemapVertexPrimvar(
    VtValue& value, const VtIntArray& vertexCounts, const VtIntArray& arnoldVertexCounts, int numPerVertex)
{
    if (!value.IsHolding<VtArray<T>>()) {
        return false;
    }
    const auto numVertexCounts = arnoldVertexCounts.size();
    if (Ai_unlikely(vertexCounts.size() != numVertexCounts)) {
        return true;
    }
    const auto& original = value.UncheckedGet<VtArray<T>>();
    VtArray<T> remapped(numPerVertex);
    const auto* originalP = original.data();
    auto* remappedP = remapped.data();
    // We use the first and the last item for each curve and using the CanInterpolate type.
    // - Interpolate values if we can interpolate the type.
    // - Look for the closest one if we can't interpolate the type.
    for (auto curve = decltype(numVertexCounts){0}; curve < numVertexCounts; curve += 1)
    {
        const auto originalVertexCount = vertexCounts[curve];
        const auto arnoldVertexCount = arnoldVertexCounts[curve];
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

// We need two fixed template arguments here to avoid ambiguity with the templated function above.
template <typename T0, typename T1, typename... T>
inline bool _RemapVertexPrimvar(
    VtValue& value, const VtIntArray& vertexCounts, const VtIntArray& arnoldVertexCounts, int numPerVertex)
{
    return _RemapVertexPrimvar<T0>(value, vertexCounts, arnoldVertexCounts, numPerVertex) ||
           _RemapVertexPrimvar<T1, T...>(value, vertexCounts, arnoldVertexCounts, numPerVertex);
}

} // namespace

HdArnoldBasisCurves::HdArnoldBasisCurves(
    HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId)
    : HdBasisCurves(id, instancerId), _shape(str::curves, delegate, id, GetPrimId()), _interpolation(HdTokens->linear)
{
}

void HdArnoldBasisCurves::Sync(
    HdSceneDelegate* delegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    TF_UNUSED(reprToken);
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    const auto& id = GetId();

    // Points can either come through accessing HdTokens->points, or driven by UsdSkel.
    const auto dirtyPrimvars = HdArnoldGetComputedPrimvars(delegate, id, *dirtyBits, _primvars) ||
                               (*dirtyBits & HdChangeTracker::DirtyPrimvar);
    if (_primvars.count(HdTokens->points) == 0 && HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        param->Interrupt();
        HdArnoldSetPositionFromPrimvar(_shape.GetShape(), id, delegate, str::points);
    }

    if (_primvars.count(HdTokens->widths) == 0 && HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)) {
        param->Interrupt();
        HdArnoldSetRadiusFromPrimvar(_shape.GetShape(), id, delegate);
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        param->Interrupt();
        const auto topology = GetBasisCurvesTopology(delegate);
        const auto curveBasis = topology.GetCurveBasis();
        const auto curveType = topology.GetCurveType();
        if (curveType == HdTokens->linear) {
            AiNodeSetStr(_shape.GetShape(), str::basis, str::linear);
            _interpolation = HdTokens->linear;
        } else {
            if (curveBasis == HdTokens->bezier) {
                AiNodeSetStr(_shape.GetShape(), str::basis, str::bezier);
                _interpolation = HdTokens->bezier;
            } else if (curveBasis == HdTokens->bSpline) {
                AiNodeSetStr(_shape.GetShape(), str::basis, str::b_spline);
                _interpolation = HdTokens->bSpline;
            } else if (curveBasis == HdTokens->catmullRom) {
                AiNodeSetStr(_shape.GetShape(), str::basis, str::catmull_rom);
                _interpolation = HdTokens->catmullRom;
            } else {
                AiNodeSetStr(_shape.GetShape(), str::basis, str::linear);
                _interpolation = HdTokens->linear;
            }
        }
        const auto& vertexCounts = topology.GetCurveVertexCounts();
        // When interpolation is linear, we clear out stored vertex counts, because we don't need them anymore.
        // Otherwise we need to store vertex counts for remapping primvars.
        if (_interpolation == HdTokens->linear) {
            decltype(_vertexCounts){}.swap(_vertexCounts);
        } else {
            _vertexCounts = vertexCounts;
        }
        const auto numVertexCounts = vertexCounts.size();
        auto* numPointsArray = AiArrayAllocate(numVertexCounts, 1, AI_TYPE_UINT);
        auto* numPoints = static_cast<uint32_t*>(AiArrayMap(numPointsArray));
        std::transform(vertexCounts.cbegin(), vertexCounts.cend(), numPoints, [](const int i) -> uint32_t {
            return static_cast<uint32_t>(i);
        });
        AiArrayUnmap(numPointsArray);
        AiNodeSetArray(_shape.GetShape(), str::num_points, numPointsArray);
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        param->Interrupt();
        _UpdateVisibility(delegate, dirtyBits);
        _shape.SetVisibility(_sharedData.visible ? AI_RAY_ALL : uint8_t{0});
    }

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param->Interrupt();
        HdArnoldSetTransform(_shape.GetShape(), delegate, GetId());
        transformDirtied = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        param->Interrupt();
        const auto* material = reinterpret_cast<const HdArnoldMaterial*>(
            delegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, delegate->GetMaterialId(id)));
        if (material != nullptr) {
            AiNodeSetPtr(_shape.GetShape(), str::shader, material->GetSurfaceShader());
        } else {
            AiNodeSetPtr(_shape.GetShape(), str::shader, _shape.GetDelegate()->GetFallbackShader());
        }
    }

    if (dirtyPrimvars) {
        HdArnoldGetPrimvars(delegate, id, *dirtyBits, false, _primvars);
        param->Interrupt();
        auto visibility = _shape.GetVisibility();
        const auto vstep = _interpolation == HdTokens->bezier ? 3 : 1;
        const auto vmin = _interpolation == HdTokens->linear ? 2 : 4;
        // TODO(pal): Should we cache these?
        // We are pre-calculating the per vertex counts for the Arnold curves object, which is different
        // from USD's.
        // Arnold only supports per segment user data, so we need to precalculate.
        // Arnold always requires segment + 1 number of user data per each curve.
        // For linear curves, the number of user data is always the same as the number of vertices.
        // For non-linear curves, we can use vstep and vmin to calculate it.
        VtIntArray arnoldVertexCounts;
        int numPerVertex = 0;
        auto setArnoldVertexCounts = [&]() {
            if (arnoldVertexCounts.empty()) {
                const auto numVertexCounts = _vertexCounts.size();
                arnoldVertexCounts.resize(numVertexCounts);
                for (auto i = decltype(numVertexCounts){0}; i < numVertexCounts; i += 1) {
                    const auto numSegments = (_vertexCounts[i] - vmin) / vstep + 1;
                    arnoldVertexCounts[i] = numSegments + 1;
                    numPerVertex += numSegments + 1;
                }
            }
        };
        for (const auto& primvar : _primvars) {
            const auto& desc = primvar.second;
            if (!desc.dirtied) {
                continue;
            }

            if (primvar.first == HdTokens->widths || primvar.first == _tokens->pscale) {
                if (desc.interpolation == HdInterpolationVertex && _interpolation != HdTokens->linear) {
                    auto value = desc.value;
                    setArnoldVertexCounts();
                    // Remapping the per vertex parameters to match the arnold requirements.
                    _RemapVertexPrimvar<float, double>(value, _vertexCounts, arnoldVertexCounts, numPerVertex);
                    HdArnoldSetRadiusFromValue(_shape.GetShape(), value);
                } else {
                    HdArnoldSetRadiusFromValue(_shape.GetShape(), desc.value);
                }
                // For constant and
            } else if (desc.interpolation == HdInterpolationConstant) {
                // We skip reading the basis for now as it would require remapping the vertices, widths and
                // all the primvars.
                if (primvar.first != str::t_basis) {
                    HdArnoldSetConstantPrimvar(_shape.GetShape(), primvar.first, desc.role, desc.value, &visibility);
                }
            } else if (desc.interpolation == HdInterpolationUniform) {
                HdArnoldSetUniformPrimvar(_shape.GetShape(), primvar.first, desc.role, desc.value);
            } else if (desc.interpolation == HdInterpolationVertex) {
                if (primvar.first == HdTokens->points) {
                    HdArnoldSetPositionFromValue(_shape.GetShape(), str::curves, desc.value);
                } else {
                    auto value = desc.value;
                    if (_interpolation != HdTokens->linear) {
                        setArnoldVertexCounts();
                        // Remapping the per vertex parameters to match the arnold requirements.
                        _RemapVertexPrimvar<
                            bool, VtUCharArray::value_type, unsigned int, int, float, GfVec2f, GfVec3f, GfVec4f,
                            std::string, TfToken, SdfAssetPath>(value, _vertexCounts, arnoldVertexCounts, numPerVertex);
                    }
                    HdArnoldSetVertexPrimvar(_shape.GetShape(), primvar.first, desc.role, value);
                }
            } else if (desc.interpolation == HdInterpolationVarying) {
                HdArnoldSetVertexPrimvar(_shape.GetShape(), primvar.first, desc.role, desc.value);
            }
        }
        _shape.SetVisibility(visibility);
    }

    _shape.Sync(this, *dirtyBits, delegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldBasisCurves::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology |
           HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility | HdChangeTracker::DirtyPrimvar |
           HdChangeTracker::DirtyNormals | HdChangeTracker::DirtyWidths | HdChangeTracker::DirtyInstancer |
           HdChangeTracker::DirtyMaterialId;
}

HdDirtyBits HdArnoldBasisCurves::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits & HdChangeTracker::AllDirty;
}

void HdArnoldBasisCurves::_InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits)
{
    TF_UNUSED(reprToken);
    TF_UNUSED(dirtyBits);
}

PXR_NAMESPACE_CLOSE_SCOPE
