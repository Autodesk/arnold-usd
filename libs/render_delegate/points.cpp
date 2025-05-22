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

PXR_NAMESPACE_OPEN_SCOPE

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

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        param.Interrupt();
        const auto materialId = sceneDelegate->GetMaterialId(id);
        // Ensure the reference from this shape to its material is properly tracked
        // by the render delegate
        GetRenderDelegate()->TrackDependencies(id, HdArnoldRenderDelegate::PathSetWithDirtyBits {{materialId, HdChangeTracker::DirtyMaterialId}});
        const auto* material = HdArnoldNodeGraph::GetNodeGraph(sceneDelegate->GetRenderIndex(), materialId);

        if (material != nullptr) {
            AiNodeSetPtr(node, str::shader, material->GetSurfaceShader());
        } else {
            AiNodeSetPtr(node, str::shader, GetRenderDelegate()->GetFallbackSurfaceShader());
        }
    }

    auto extrapolatePoints = false;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdArnoldGetPrimvars(sceneDelegate, id, *dirtyBits, _primvars);
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        param.Interrupt();
        for (auto& primvar : _primvars) {
            auto& desc = primvar.second;
            // We can use this information to reset built-in values to their default values.
            if (!desc.NeedsUpdate()) {
                continue;
            }

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
                    HdArnoldSetUniformPrimvar(node, primvar.first, desc.role, desc.value, GetRenderDelegate());
                }
            }
        }

        UpdateVisibilityAndSidedness();
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
