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

#include <constant_strings.h>
#include <shape_utils.h>

#include "node_graph.h"
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
    ((basis, "arnold:basis"))
);
// clang-format on

namespace {

} // namespace

#if PXR_VERSION >= 2102
HdArnoldBasisCurves::HdArnoldBasisCurves(HdArnoldRenderDelegate* delegate, const SdfPath& id)
    : HdArnoldRprim<HdBasisCurves>(str::curves, delegate, id), _interpolation(HdTokens->linear)
{
}
#else
HdArnoldBasisCurves::HdArnoldBasisCurves(
    HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId)
    : HdArnoldRprim<HdBasisCurves>(str::curves, delegate, id, instancerId), _interpolation(HdTokens->linear)
{
}
#endif

void HdArnoldBasisCurves::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    TF_UNUSED(reprToken);
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();

    HdArnoldSampledPrimvarType pointsSample;
    // Points can either come through accessing HdTokens->points, or driven by UsdSkel.
    const auto dirtyPrimvars = HdArnoldGetComputedPrimvars(sceneDelegate, id, *dirtyBits, _primvars, nullptr, &pointsSample) ||
                               (*dirtyBits & HdChangeTracker::DirtyPrimvar);
    if (_primvars.count(HdTokens->points) == 0 && HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        param.Interrupt();
        HdArnoldSetPositionFromPrimvar(GetArnoldNode(), id, sceneDelegate, str::points, param(), GetDeformKeys(), &_primvars, &pointsSample);
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        param.Interrupt();
        const auto topology = GetBasisCurvesTopology(sceneDelegate);
        const auto curveBasis = topology.GetCurveBasis();
        const auto curveType = topology.GetCurveType();
        const auto curveWrap = topology.GetCurveWrap();
        if (curveType == HdTokens->linear) {
            AiNodeSetStr(GetArnoldNode(), str::basis, str::linear);
            _interpolation = HdTokens->linear;
        } else {
            if (curveBasis == HdTokens->bezier) {
                AiNodeSetStr(GetArnoldNode(), str::basis, str::bezier);
                _interpolation = HdTokens->bezier;
            } else if (curveBasis == HdTokens->bSpline) {
                AiNodeSetStr(GetArnoldNode(), str::basis, str::b_spline);
                _interpolation = HdTokens->bSpline;
            } else if (curveBasis == HdTokens->catmullRom) {
                AiNodeSetStr(GetArnoldNode(), str::basis, str::catmull_rom);
                _interpolation = HdTokens->catmullRom;
            } else {
                AiNodeSetStr(GetArnoldNode(), str::basis, str::linear);
                _interpolation = HdTokens->linear;
            }
#if ARNOLD_VERSION_NUMBER >= 70103
            if (curveBasis == HdTokens->bSpline || curveBasis == HdTokens->catmullRom)
                AiNodeSetStr(GetArnoldNode(), str::wrap_mode, AtString{curveWrap.GetText()});
#endif
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
        AiNodeSetArray(GetArnoldNode(), str::num_points, numPointsArray);
    }

    CheckVisibilityAndSidedness(sceneDelegate, id, dirtyBits, param);

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        HdArnoldSetTransform(GetArnoldNode(), sceneDelegate, GetId());
        transformDirtied = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        param.Interrupt();
        const auto materialId = sceneDelegate->GetMaterialId(id);
        // Ensure the reference from this shape to its material is properly tracked
        // by the render delegate
        GetRenderDelegate()->TrackDependencies(id, HdArnoldRenderDelegate::PathSet {materialId});

        const auto* material = reinterpret_cast<const HdArnoldNodeGraph*>(
            sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));
        if (material != nullptr) {
            AiNodeSetPtr(GetArnoldNode(), str::shader, material->GetSurfaceShader());
        } else {
            AiNodeSetPtr(GetArnoldNode(), str::shader, GetRenderDelegate()->GetFallbackSurfaceShader());
        }
    }

    if (dirtyPrimvars) {
        HdArnoldGetPrimvars(sceneDelegate, id, *dirtyBits, false, _primvars);
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        param.Interrupt();
        const auto vstep = _interpolation == HdTokens->bezier ? 3 : 1;
        const auto vmin = _interpolation == HdTokens->linear ? 2 : 4;

        ArnoldUsdCurvesData curvesData(vmin, vstep, _vertexCounts);

        // For pinned curves, we might have to remap primvars differently #1240
        bool isPinned = (AiNodeGetStr(GetArnoldNode(), str::wrap_mode) == str::pinned);

        for (auto& primvar : _primvars) {
            auto& desc = primvar.second;
            if (!desc.NeedsUpdate()) {
                continue;
            }

            if (primvar.first == HdTokens->widths) {
                // For pinned curves, vertex interpolation primvars shouldn't be remapped
                if (((desc.interpolation == HdInterpolationVertex && !isPinned)|| 
                    desc.interpolation == HdInterpolationVarying) &&
                    _interpolation != HdTokens->linear) {
                    auto value = desc.value;
                    curvesData.RemapCurvesVertexPrimvar<float, double, GfHalf>(value);
                    ArnoldUsdCurvesData::SetRadiusFromValue(GetArnoldNode(), value);
                } else {
                    ArnoldUsdCurvesData::SetRadiusFromValue(GetArnoldNode(), desc.value);
                }
                // For constant and
            } else if (desc.interpolation == HdInterpolationConstant) {
                // We skip reading the basis for now as it would require remapping the vertices, widths and
                // all the primvars.
                if (primvar.first != _tokens->basis) {
                    HdArnoldSetConstantPrimvar(
                        GetArnoldNode(), primvar.first, desc.role, desc.value, &_visibilityFlags, &_sidednessFlags,
                        nullptr);
                }
            } else if (desc.interpolation == HdInterpolationUniform) {
                if (primvar.first == str::t_uv || primvar.first == str::t_st) {
                    // This is either a VtVec2fArray or VtVec3fArray (in Solaris).
                    if (desc.value.IsHolding<VtVec2fArray>()) {
                        const auto& v = desc.value.UncheckedGet<VtVec2fArray>();
                        AiNodeSetArray(
                            GetArnoldNode(), str::uvs, AiArrayConvert(v.size(), 1, AI_TYPE_VECTOR2, v.data()));
                    } else if (desc.value.IsHolding<VtVec3fArray>()) {
                        const auto& v = desc.value.UncheckedGet<VtVec3fArray>();
                        auto* arr = AiArrayAllocate(v.size(), 1, AI_TYPE_VECTOR2);
                        std::transform(
                            v.begin(), v.end(), static_cast<GfVec2f*>(AiArrayMap(arr)),
                            [](const GfVec3f& in) -> GfVec2f {
                                return {in[0], in[1]};
                            });
                        AiArrayUnmap(arr);
                        AiNodeSetArray(GetArnoldNode(), str::uvs, arr);
                    } else {
                        // If it's an unsupported type, just set it as user data.
                        HdArnoldSetUniformPrimvar(GetArnoldNode(), primvar.first, desc.role, desc.value);
                    }
                } else {
                    HdArnoldSetUniformPrimvar(GetArnoldNode(), primvar.first, desc.role, desc.value);
                }
            } else if (desc.interpolation == HdInterpolationVertex || desc.interpolation == HdInterpolationVarying) {
                if (primvar.first == HdTokens->points) {
                    HdArnoldSetPositionFromValue(GetArnoldNode(), str::curves, desc.value);
                } else if (primvar.first == HdTokens->normals) {
                    // This should be the same number as points.
                    HdArnoldSetPositionFromValue(GetArnoldNode(), str::orientations, desc.value);
                } else {
                    auto value = desc.value;
                    // For pinned curves, vertex interpolation primvars shouldn't be remapped
                    if (_interpolation != HdTokens->linear && 
                        !(isPinned && desc.interpolation == HdInterpolationVertex)) {
                        curvesData.RemapCurvesVertexPrimvar<
                            bool, VtUCharArray::value_type, unsigned int, int, float, GfVec2f, GfVec3f, GfVec4f,
                            std::string, TfToken, SdfAssetPath>(value);
                    }
                    HdArnoldSetVertexPrimvar(GetArnoldNode(), primvar.first, desc.role, value);
                }
            } else if (desc.interpolation == HdInterpolationVarying) {
                HdArnoldSetVertexPrimvar(GetArnoldNode(), primvar.first, desc.role, desc.value);
            }
        }
        UpdateVisibilityAndSidedness();
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldBasisCurves::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology |
           HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility | HdChangeTracker::DirtyDoubleSided |
           HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyNormals | HdChangeTracker::DirtyWidths |
           HdChangeTracker::DirtyMaterialId | HdArnoldShape::GetInitialDirtyBitsMask();
}

PXR_NAMESPACE_CLOSE_SCOPE
