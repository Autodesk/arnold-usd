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
#include "gaussian_splat.h"

#if PXR_VERSION >= 2603

#include <constant_strings.h>

#include "node_graph.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (positions)
    (orientations)
    (scales)
    (opacities)
    ((shCoeffs, "radiance:sphericalHarmonicsCoefficients"))
    ((shDegree,  "radiance:sphericalHarmonicsDegree"))
);
// clang-format on

HdArnoldGaussianSplat::HdArnoldGaussianSplat(
    HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdArnoldRprim<HdRprim>(str::points, renderDelegate, id)
{
    // Enable gaussian splat rendering mode immediately so it is set even if
    // no Sync has been called yet.
    AiNodeSetStr(GetArnoldNode(), str::mode, str::gaussian);
}

HdDirtyBits HdArnoldGaussianSplat::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility |
           HdChangeTracker::DirtyDoubleSided | HdChangeTracker::DirtyPrimvar |
           HdChangeTracker::DirtyMaterialId | HdArnoldShape::GetInitialDirtyBitsMask();
}

namespace {

// Arnold's gaussianEvalSh expects the DC SH coefficient (index 0 per particle)
// to be pre-stored as (raw_f_dc * C0 + 0.5), where C0 = 0.28209479177387814.
// USD radiance:sphericalHarmonicsCoefficients stores raw SH coefficients; this
// function applies the required transformation in-place on the gs_sh AtArray.
static void _NormalizeShDcCoefficient(AtNode* node)
{
    AtArray* gsShArr  = AiNodeGetArray(node, str::gs_sh);
    AtArray* ptsArr   = AiNodeGetArray(node, str::points);
    if (!gsShArr || !ptsArr)
        return;
    const size_t nPts         = AiArrayGetNumElements(ptsArr);
    const size_t nTotalCoeffs = AiArrayGetNumElements(gsShArr);
    if (nPts == 0 || nTotalCoeffs % nPts != 0)
        return;
    const size_t nPerPoint = nTotalCoeffs / nPts;
    // Valid SH coefficient counts per point: 1 (deg 0), 4, 9, 16 (deg 1-3)
    if (nPerPoint != 1 && nPerPoint != 4 && nPerPoint != 9 && nPerPoint != 16)
        return;
    constexpr float C0 = 0.28209479177387814f;
    for (size_t i = 0; i < nPts; ++i) {
        const size_t dcIdx = i * nPerPoint;
        const AtRGB  dc    = AiArrayGetRGB(gsShArr, dcIdx);
        AiArraySetRGB(gsShArr, dcIdx,
            AtRGB(dc.r * C0 + 0.5f, dc.g * C0 + 0.5f, dc.b * C0 + 0.5f));
    }
}

// Set Arnold gs_rotation from a VtQuatfArray.
// Arnold stores quaternions as [x, y, z, w] (4 floats per point).
// USD GfQuatf stores GetReal()=w and GetImaginary()=(x,y,z).
void _SetRotationFromQuatf(AtNode* node, const VtQuatfArray& orientations)
{
    const size_t n = orientations.size();
    if (n == 0)
        return;
    AtArray* rotArray = AiArrayAllocate(n * 4, 1, AI_TYPE_FLOAT);
    float* out = static_cast<float*>(AiArrayMap(rotArray));
    for (size_t i = 0; i < n; ++i) {
        const GfVec3f& imag = orientations[i].GetImaginary();
        const float    w    = orientations[i].GetReal();
        out[i * 4 + 0] = imag[0]; // x
        out[i * 4 + 1] = imag[1]; // y
        out[i * 4 + 2] = imag[2]; // z
        out[i * 4 + 3] = w;       // w
    }
    AiArrayUnmap(rotArray);
    AiNodeSetArray(node, str::gs_rotation, rotArray);
}

} // namespace

void HdArnoldGaussianSplat::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
    const TfToken& reprToken)
{
    if (!GetRenderDelegate()->CanUpdateScene())
        return;

    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id   = GetId();
    AtNode*     node = GetArnoldNode();

    if (SkipHiddenPrim(sceneDelegate, id, dirtyBits, param))
        return;

    bool transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        HdArnoldRenderParam* arnoldRenderParam =
            reinterpret_cast<HdArnoldRenderParam*>(renderParam);
        HdArnoldSetTransform(node, sceneDelegate, GetId(), arnoldRenderParam->GetShutterRange());
        transformDirtied = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyCategories) {
        param.Interrupt();
        GetRenderDelegate()->ApplyLightLinking(sceneDelegate, node, GetId());
    }

    CheckVisibilityAndSidedness(sceneDelegate, id, dirtyBits, param);

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdArnoldGetPrimvars(sceneDelegate, id, *dirtyBits, _primvars);
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        param.Interrupt();

        bool shCoeffsUpdated = false;
        for (auto& primvar : _primvars) {
            auto&         desc = primvar.second;
            if (!desc.NeedsUpdate())
                continue;

            const TfToken& name = primvar.first;

            // --- positions → Arnold "points" ----------------------------------
            if (name == _tokens->positions) {
                if (desc.value.IsHolding<VtVec3fArray>()) {
                    const auto& v = desc.value.UncheckedGet<VtVec3fArray>();
                    if (!v.empty())
                        AiNodeSetArray(node, str::points,
                            AiArrayConvert(v.size(), 1, AI_TYPE_VECTOR, v.cdata()));
                } else if (desc.value.IsHolding<VtVec3hArray>()) {
                    const auto& vh = desc.value.UncheckedGet<VtVec3hArray>();
                    if (!vh.empty()) {
                        std::vector<GfVec3f> v(vh.size());
                        for (size_t i = 0; i < vh.size(); ++i)
                            v[i] = GfVec3f(vh[i]);
                        AiNodeSetArray(node, str::points,
                            AiArrayConvert(v.size(), 1, AI_TYPE_VECTOR, v.data()));
                    }
                }
                continue;
            }

            // --- scales → Arnold "gs_scale" -----------------------------------
            if (name == _tokens->scales) {
                if (desc.value.IsHolding<VtVec3fArray>()) {
                    const auto& v = desc.value.UncheckedGet<VtVec3fArray>();
                    if (!v.empty())
                        AiNodeSetArray(node, str::gs_scale,
                            AiArrayConvert(v.size(), 1, AI_TYPE_VECTOR, v.cdata()));
                } else if (desc.value.IsHolding<VtVec3hArray>()) {
                    const auto& vh = desc.value.UncheckedGet<VtVec3hArray>();
                    if (!vh.empty()) {
                        std::vector<GfVec3f> v(vh.size());
                        for (size_t i = 0; i < vh.size(); ++i)
                            v[i] = GfVec3f(vh[i]);
                        AiNodeSetArray(node, str::gs_scale,
                            AiArrayConvert(v.size(), 1, AI_TYPE_VECTOR, v.data()));
                    }
                }
                continue;
            }

            // --- orientations → Arnold "gs_rotation" ([x,y,z,w] per splat) ---
            if (name == _tokens->orientations) {
                if (desc.value.IsHolding<VtQuatfArray>()) {
                    _SetRotationFromQuatf(node, desc.value.UncheckedGet<VtQuatfArray>());
                } else if (desc.value.IsHolding<VtQuathArray>()) {
                    const auto& vh = desc.value.UncheckedGet<VtQuathArray>();
                    if (!vh.empty()) {
                        VtQuatfArray v(vh.size());
                        for (size_t i = 0; i < vh.size(); ++i)
                            v[i] = GfQuatf(vh[i]);
                        _SetRotationFromQuatf(node, v);
                    }
                }
                continue;
            }

            // --- opacities → Arnold "gs_opacity" ------------------------------
            if (name == _tokens->opacities) {
                if (desc.value.IsHolding<VtFloatArray>()) {
                    const auto& v = desc.value.UncheckedGet<VtFloatArray>();
                    if (!v.empty())
                        AiNodeSetArray(node, str::gs_opacity,
                            AiArrayConvert(v.size(), 1, AI_TYPE_FLOAT, v.cdata()));
                } else if (desc.value.IsHolding<VtHalfArray>()) {
                    const auto& vh = desc.value.UncheckedGet<VtHalfArray>();
                    if (!vh.empty()) {
                        std::vector<float> v(vh.size());
                        for (size_t i = 0; i < vh.size(); ++i)
                            v[i] = static_cast<float>(vh[i]);
                        AiNodeSetArray(node, str::gs_opacity,
                            AiArrayConvert(v.size(), 1, AI_TYPE_FLOAT, v.data()));
                    }
                }
                continue;
            }

            // --- SH coefficients → Arnold "gs_sh" ----------------------------
            // Note: USD stores raw SH coefficients. Arnold's gaussianEvalSh
            // expects the DC term (index 0 per particle) pre-transformed as
            // (raw_dc * C0 + 0.5). We apply this in _NormalizeShDcCoefficient
            // after the full primvar loop once the point count is also known.
            if (name == _tokens->shCoeffs) {
                if (desc.value.IsHolding<VtVec3fArray>()) {
                    const auto& v = desc.value.UncheckedGet<VtVec3fArray>();
                    if (!v.empty()) {
                        AiNodeSetArray(node, str::gs_sh,
                            AiArrayConvert(v.size(), 1, AI_TYPE_RGB, v.cdata()));
                        shCoeffsUpdated = true;
                    }
                } else if (desc.value.IsHolding<VtVec3hArray>()) {
                    const auto& vh = desc.value.UncheckedGet<VtVec3hArray>();
                    if (!vh.empty()) {
                        std::vector<GfVec3f> v(vh.size());
                        for (size_t i = 0; i < vh.size(); ++i)
                            v[i] = GfVec3f(vh[i]);
                        AiNodeSetArray(node, str::gs_sh,
                            AiArrayConvert(v.size(), 1, AI_TYPE_RGB, v.data()));
                        shCoeffsUpdated = true;
                    }
                }
                continue;
            }

            // --- SH degree — skipped, Arnold infers degree from gs_sh count ---
            if (name == _tokens->shDegree)
                continue;

            // --- generic fall-through primvar handling ------------------------
            if (desc.interpolation == HdInterpolationConstant) {
                HdArnoldSetConstantPrimvar(node, name, desc.role, desc.value,
                    &_visibilityFlags, &_sidednessFlags, nullptr, GetRenderDelegate());
            } else if (desc.interpolation != HdInterpolationInstance) {
                HdArnoldSetUniformPrimvar(node, name, desc.role, desc.value,
                    &desc.valueIndices, GetRenderDelegate());
            }
        }

        if (shCoeffsUpdated)
            _NormalizeShDcCoefficient(node);

        UpdateVisibilityAndSidedness();
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        param.Interrupt();
        const auto materialId = sceneDelegate->GetMaterialId(id);
        GetRenderDelegate()->TrackDependencies(
            id, HdArnoldRenderDelegate::PathSetWithDirtyBits{
                    {materialId, HdChangeTracker::DirtyMaterialId}});

        const auto* material = reinterpret_cast<const HdArnoldNodeGraph*>(
            sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));
        if (material != nullptr) {
            AiNodeSetPtr(node, str::shader, material->GetCachedSurfaceShader());
        } else {
            // When no material is assigned, fall back to a shared gaussian_splat_shader.
            // Look for an existing one first to avoid duplicates.
            AtUniverse* universe = AiNodeGetUniverse(node);
            AtNode* gsShader = AiNodeLookUpByName(universe, str::gaussian_splat_shader);
            if (gsShader == nullptr) {
                gsShader = AiNode(universe, str::gaussian_splat_shader, str::gaussian_splat_shader);
            }
            AiNodeSetPtr(node, str::shader, gsShader);
        }
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);
    *dirtyBits = HdChangeTracker::Clean;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_VERSION >= 2603
