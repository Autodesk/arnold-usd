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

PXR_NAMESPACE_OPEN_SCOPE

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
        // We are pre-calculating the per vertex and varying counts for the Arnold curves object, which is different
        // from USD's.
        // Arnold only supports varying (per segment) user data, so we need to precalculate.
        // Arnold always requires segment + 1 number of user data per each curve.
        // For linear curves, the number of user data is always the same as the number of vertices.
        // For non-linear curves, we can use vstep and vmin to calculate it.
        VtIntArray arnoldVaryingCounts;
        auto setArnoldVaryingCounts = [&]() {
            if (arnoldVaryingCounts.empty()) {
                arnoldVaryingCounts.resize(_vertexCounts.size());
                std::transform(_vertexCounts.begin(), _vertexCounts.end(), arnoldVaryingCounts.begin(), [&](const int numCV) -> int {
                    return (numCV - vmin) / vstep + 1;
                });
            }
        };
        for (const auto& primvar : _primvars) {
            const auto& desc = primvar.second;
            if (!desc.dirtied) {
                continue;
            }

            // For constant and
            if (desc.interpolation == HdInterpolationConstant) {
                if (primvar.first == HdTokens->widths) {
                    HdArnoldSetRadiusFromValue(_shape.GetShape(), desc.value);
                } else {
                    HdArnoldSetConstantPrimvar(_shape.GetShape(), primvar.first, desc.role, desc.value, &visibility);
                }
            } else if (desc.interpolation == HdInterpolationUniform) {
                if (primvar.first == HdTokens->widths) {
                    HdArnoldSetRadiusFromValue(_shape.GetShape(), desc.value);
                } else {
                    HdArnoldSetUniformPrimvar(_shape.GetShape(), primvar.first, desc.role, desc.value);
                }
            } else if (desc.interpolation == HdInterpolationVertex) {
                if (primvar.first == HdTokens->points) {
                    HdArnoldSetPositionFromValue(_shape.GetShape(), str::curves, desc.value);
                } else {
                    if (_interpolation != HdTokens->linear) {
                        setArnoldVaryingCounts();
                        // Remapping the per vertex parameters to match the arnold requirements.
                    }
                    if (primvar.first == HdTokens->widths) {
                        HdArnoldSetRadiusFromValue(_shape.GetShape(), desc.value);
                    } else {
                        HdArnoldSetVertexPrimvar(_shape.GetShape(), primvar.first, desc.role, desc.value);
                    }
                }
            } else if (desc.interpolation == HdInterpolationVarying) {
                if (_interpolation != HdTokens->linear) {
                    setArnoldVaryingCounts();
                    // Remapping the varying parameters to match the arnold requirements.
                }
            } else if (desc.interpolation == HdInterpolationInstance) {
                // TODO (pal): Add new functions to the instance class to read per instance data.
                //  See https://github.com/Autodesk/arnold-usd/issues/471
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
