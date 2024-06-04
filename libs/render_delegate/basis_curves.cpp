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
#include "basis_curves.h"

#include <constant_strings.h>
#include <shape_utils.h>

#include "node_graph.h"
#include "utils.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>

#include <pxr/usd/sdf/assetPath.h>
#include <ai.h>

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


// Function to convert a GfVec3f array to a GfVec2f array, skipping the last component.
// It could be used more generally later by registering it as a VtValue Cast:
//
//     VtValue::RegisterCast<VtVec3fArray, VtVec2fArray>(&Vec3fToVec2f);
//
//  then simply call the Cast function as follows:
//
//     value = VtValue::Cast<VtVec2fArray>(desc.value);
//
VtValue Vec3fToVec2f(VtValue const &val) {
    if (val.IsHolding<VtVec3fArray>()) {
        const auto& vec3 = val.UncheckedGet<VtVec3fArray>();
        VtVec2fArray vec2(vec3.size());
        std::transform(
            vec3.cbegin(), vec3.cend(), vec2.begin(),
            [](const GfVec3f& in) -> GfVec2f {
                return {in[0], in[1]};
            });
        return VtValue::Take(vec2);
    }
    return {};
}

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
    bool dirtyTopology = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
    bool dirtyPrimvars = HdArnoldGetComputedPrimvars(sceneDelegate, id, *dirtyBits, _primvars, nullptr, &pointsSample) ||
                               (*dirtyBits & HdChangeTracker::DirtyPrimvar);
    bool dirtyPoints = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points);
    
    TfToken curveType;
    HdBasisCurvesTopology topology;

    if (dirtyTopology || dirtyPoints || dirtyPrimvars) {
        // If topology / points / primvars have changed and this curve has a linear basis
        // then we need to ensure all of these attributes are updated, because
        // Arnold converts linear curves to bezier on the fly #1861
        topology = GetBasisCurvesTopology(sceneDelegate);
        curveType = topology.GetCurveType();
        if (curveType == HdTokens->linear) {
            dirtyTopology = dirtyPoints = dirtyPrimvars = true;
        }
    }

    // Points can either come through accessing HdTokens->points, or driven by UsdSkel.
    // If we already have a primvar for points, it will be translated below, in the 
    // primvars conversion section
    if (dirtyPoints && _primvars.count(HdTokens->points) == 0) {
        param.Interrupt();
        HdArnoldSetPositionFromPrimvar(GetArnoldNode(), id, sceneDelegate, str::points, param(), GetDeformKeys(), &_primvars, &pointsSample);
    }

    if (dirtyTopology) {
        param.Interrupt();
        const auto curveBasis = topology.GetCurveBasis();            
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
#if ARNOLD_VERSION_NUM >= 70103
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
        if (numVertexCounts > 0) {
            auto* numPoints = static_cast<uint32_t*>(AiArrayMap(numPointsArray));
            std::transform(vertexCounts.cbegin(), vertexCounts.cend(), numPoints, [](const int i) -> uint32_t {
                return static_cast<uint32_t>(i);
            });
            AiArrayUnmap(numPointsArray);
        }
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
        GetRenderDelegate()->TrackDependencies(id, HdArnoldRenderDelegate::PathSetWithDirtyBits {{materialId, HdChangeTracker::DirtyMaterialId}});

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
                continue;
            }

            // The curves node only knows the "uvs" parameter, so we have to rename the attribute
            TfToken arnoldAttributeName = primvar.first;
            auto value = desc.value;
            if (primvar.first == str::t_uv || primvar.first == str::t_st) {
                arnoldAttributeName = str::t_uvs;
                // Special case if the uvs attribute has 3 dimensions
                if (desc.value.IsHolding<VtVec3fArray>()) {
                    value = Vec3fToVec2f(desc.value);
                }
            }
            
            if (desc.interpolation == HdInterpolationConstant) {
                // We skip reading the basis for now as it would require remapping the vertices, widths and
                // all the primvars.
                if (primvar.first != _tokens->basis) {
                    HdArnoldSetConstantPrimvar(
                        GetArnoldNode(), arnoldAttributeName, desc.role, value, &_visibilityFlags, &_sidednessFlags,
                        nullptr, GetRenderDelegate());
                }
            } else if (desc.interpolation == HdInterpolationUniform) {
                HdArnoldSetUniformPrimvar(GetArnoldNode(), arnoldAttributeName, desc.role, value, GetRenderDelegate());
            } else if (desc.interpolation == HdInterpolationVertex || desc.interpolation == HdInterpolationVarying) {
                if (primvar.first == HdTokens->points) {
                    HdArnoldSetPositionFromValue(GetArnoldNode(), str::points, value);
                } else if (primvar.first == HdTokens->normals) {
                    if (_interpolation == HdTokens->linear)
                        AiMsgWarning("%s : Orientations not supported on linear curves", AiNodeGetName(GetArnoldNode()));
                    else
                        curvesData.SetOrientationFromValue(GetArnoldNode(), value);
                } else {
                    // For pinned curves, vertex interpolation primvars shouldn't be remapped
                    if (_interpolation != HdTokens->linear && 
                        !(isPinned && desc.interpolation == HdInterpolationVertex)) {
                        curvesData.RemapCurvesVertexPrimvar<
                            bool, VtUCharArray::value_type, unsigned int, int, float, GfVec2f, GfVec3f, GfVec4f,
                            std::string, TfToken, SdfAssetPath>(value);
                    }
                    HdArnoldSetVertexPrimvar(GetArnoldNode(), arnoldAttributeName, desc.role, value, GetRenderDelegate());
                }
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
