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
#include "points.h"

#include <constant_strings.h>

#include "node_graph.h"
#include "utils.h"

#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/quatf.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_gsTokens,
    (GS_Alpha)
    (displayColor)
    (orient)
    (scale)
);
// clang-format on

HdArnoldPoints::HdArnoldPoints(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdArnoldRprim<HdPoints>(str::points, renderDelegate, id)
{
}

HdDirtyBits HdArnoldPoints::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility |
           HdChangeTracker::DirtyDoubleSided | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyWidths |
           HdChangeTracker::DirtyMaterialId | HdArnoldShape::GetInitialDirtyBitsMask();
}

void HdArnoldPoints::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    if (!GetRenderDelegate()->CanUpdateScene())
        return;
 
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();
    AtNode* node = GetArnoldNode();
    
    // If the primitive is invisible for hydra, we want to skip it here
    if (SkipHiddenPrim(sceneDelegate, id, dirtyBits, param))
        return;

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        HdArnoldRenderParam * renderParam = reinterpret_cast<HdArnoldRenderParam*>(_renderDelegate->GetRenderParam());
        HdArnoldSetTransform(node, sceneDelegate, GetId(), renderParam->GetShutterRange());
        transformDirtied = true;
    }
    if (*dirtyBits & HdChangeTracker::DirtyCategories) {
        param.Interrupt();
        GetRenderDelegate()->ApplyLightLinking(sceneDelegate, node, GetId());
    }

    CheckVisibilityAndSidedness(sceneDelegate, id, dirtyBits, param);

    auto extrapolatePoints = false;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdArnoldGetPrimvars(sceneDelegate, id, *dirtyBits, _primvars);
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        param.Interrupt();

        // Detect Houdini Gaussian Splats: presence of primvars:GS_Alpha marks the
        // prim as a gaussian splat exported from Houdini.
        const bool isHoudiniGS = _primvars.count(_gsTokens->GS_Alpha) > 0;
        if (isHoudiniGS)
            AiNodeSetStr(node, str::mode, str::gaussian);

        for (auto& primvar : _primvars) {
            auto& desc = primvar.second;
            if (!desc.NeedsUpdate()) {
                continue;
            }

            const TfToken& name = primvar.first;

            if (isHoudiniGS) {
                // --- primvars:displayColor → Arnold gs_sh ----------------------
                // Houdini stores the DC SH coefficient pre-normalized (raw_dc * C0 + 0.5)
                // in displayColor, so we pass it directly without any further transform.
                if (name == _gsTokens->displayColor) {
                    if (desc.value.IsHolding<VtVec3fArray>()) {
                        const auto& v = desc.value.UncheckedGet<VtVec3fArray>();
                        if (!v.empty())
                            AiNodeSetArray(node, str::gs_sh,
                                AiArrayConvert(v.size(), 1, AI_TYPE_RGB, v.cdata()));
                    }
                    continue;
                }
                // --- primvars:GS_Alpha → Arnold gs_opacity ----------------------
                if (name == _gsTokens->GS_Alpha) {
                    if (desc.value.IsHolding<VtFloatArray>()) {
                        const auto& v = desc.value.UncheckedGet<VtFloatArray>();
                        if (!v.empty())
                            AiNodeSetArray(node, str::gs_opacity,
                                AiArrayConvert(v.size(), 1, AI_TYPE_FLOAT, v.cdata()));
                    }
                    continue;
                }
                // --- primvars:scale → Arnold gs_scale --------------------------
                if (name == _gsTokens->scale) {
                    if (desc.value.IsHolding<VtVec3fArray>()) {
                        const auto& v = desc.value.UncheckedGet<VtVec3fArray>();
                        if (!v.empty())
                            AiNodeSetArray(node, str::gs_scale,
                                AiArrayConvert(v.size(), 1, AI_TYPE_VECTOR, v.cdata()));
                    }
                    continue;
                }
                // --- primvars:orient → Arnold gs_rotation ([x,y,z,w] per splat) --
                // GfQuatf is stored as (real=w, imaginary=(x,y,z)); Arnold expects [x,y,z,w]
                if (name == _gsTokens->orient) {
                    VtQuatfArray orientations;
                    if (desc.value.IsHolding<VtQuatfArray>()) {
                        orientations = desc.value.UncheckedGet<VtQuatfArray>();
                    } else if (desc.value.IsHolding<VtQuathArray>()) {
                        const auto& vh = desc.value.UncheckedGet<VtQuathArray>();
                        orientations.resize(vh.size());
                        for (size_t i = 0; i < vh.size(); ++i)
                            orientations[i] = GfQuatf(vh[i]);
                    }
                    if (!orientations.empty()) {
                        const size_t n = orientations.size();
                        AtArray* rotArray = AiArrayAllocate(n * 4, 1, AI_TYPE_FLOAT);
                        float* out = static_cast<float*>(AiArrayMap(rotArray));
                        for (size_t i = 0; i < n; ++i) {
                            const GfVec3f& imag = orientations[i].GetImaginary();
                            const float    w    = orientations[i].GetReal();
                            out[i * 4 + 0] = imag[0];
                            out[i * 4 + 1] = imag[1];
                            out[i * 4 + 2] = imag[2];
                            out[i * 4 + 3] = w;
                        }
                        AiArrayUnmap(rotArray);
                        AiNodeSetArray(node, str::gs_rotation, rotArray);
                    }
                    continue;
                }
            } // isHoudiniGS

            if (desc.interpolation == HdInterpolationConstant) {
                if (primvar.first == str::deformKeys) {
                    if (desc.value.IsHolding<int>()) {
                        SetDeformKeys(desc.value.UncheckedGet<int>());
                    } else {
                        SetDeformKeys(-1);
                    }
                    extrapolatePoints = true;
                } else {
                    HdArnoldSetConstantPrimvar(
                        node, primvar.first, desc.role,   desc.value, &_visibilityFlags, &_sidednessFlags,
                        nullptr, GetRenderDelegate());
                }
                // Anything that's not per instance interpolation needs to be converted to uniform data.
            } else if (desc.interpolation != HdInterpolationInstance) {
                // Even though we are using velocity and acceleration for optional interpolation, we are still
                // converting the values to user data.
                if (primvar.first != HdTokens->points && primvar.first != HdTokens->widths) {
                    HdArnoldSetUniformPrimvar(node, primvar.first, desc.role, desc.value, &desc.valueIndices, GetRenderDelegate());
                }
            }
        }

        UpdateVisibilityAndSidedness();
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
            AiNodeSetPtr(node, str::shader, _IsVolume() ? material->GetCachedVolumeShader() : material->GetCachedSurfaceShader());
        } else {
            // For Houdini gaussian splats with no material bound, use gaussian_splat_shader.
            const bool isHoudiniGS = _primvars.count(_gsTokens->GS_Alpha) > 0;
            if (isHoudiniGS) {
                AtUniverse* universe = AiNodeGetUniverse(node);
                AtNode* gsShader = AiNodeLookUpByName(universe, str::gaussian_splat_shader);
                if (gsShader == nullptr)
                    gsShader = AiNode(universe, str::gaussian_splat_shader, str::gaussian_splat_shader);
                AiNodeSetPtr(node, str::shader, gsShader);
            } else {
                AiNodeSetPtr(
                    node, str::shader,
                    _IsVolume() ? GetRenderDelegate()->GetFallbackVolumeShader()
                                : GetRenderDelegate()->GetFallbackSurfaceShader());
            }
        }
    }

    if (extrapolatePoints || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        param.Interrupt();
        HdArnoldSetPositionFromPrimvar(
            node, id, sceneDelegate, str::points, param(), GetDeformKeys(), &_primvars);
    }
    // Ensure we set radius after the positions, as we might need to check the amount of points #2015
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)) {
        param.Interrupt();
        HdArnoldSetRadiusFromPrimvar(node, id, sceneDelegate);
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

PXR_NAMESPACE_CLOSE_SCOPE
