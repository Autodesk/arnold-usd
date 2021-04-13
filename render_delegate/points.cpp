// Copyright 2019 Autodesk, Inc.
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

#include "material.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

#if PXR_VERSION >= 2102
HdArnoldPoints::HdArnoldPoints(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdArnoldRprim<HdPoints>(str::points, renderDelegate, id)
{
}
#else
HdArnoldPoints::HdArnoldPoints(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const SdfPath& instancerId)
    : HdArnoldRprim<HdPoints>(str::points, renderDelegate, id, instancerId)
{
}
#endif

HdDirtyBits HdArnoldPoints::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility |
           HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyWidths | HdChangeTracker::DirtyMaterialId |
           HdArnoldShape::GetInitialDirtyBitsMask();
}

void HdArnoldPoints::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        HdArnoldSetTransform(GetArnoldNode(), sceneDelegate, GetId());
        transformDirtied = true;
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)) {
        param.Interrupt();
        HdArnoldSetRadiusFromPrimvar(GetArnoldNode(), id, sceneDelegate);
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        param.Interrupt();
        _UpdateVisibility(sceneDelegate, dirtyBits);
        AiNodeSetByte(GetArnoldNode(), str::visibility, _sharedData.visible ? AI_RAY_ALL : uint8_t{0});
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        param.Interrupt();
        const auto materialId = sceneDelegate->GetMaterialId(id);
        _materialTracker.TrackSingleMaterial(GetRenderDelegate(), id, materialId);
        const auto* material = reinterpret_cast<const HdArnoldMaterial*>(
            sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));
        if (material != nullptr) {
            AiNodeSetPtr(GetArnoldNode(), str::shader, material->GetSurfaceShader());
        } else {
            AiNodeSetPtr(GetArnoldNode(), str::shader, GetRenderDelegate()->GetFallbackShader());
        }
    }

    auto extrapolatePoints = false;
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        param.Interrupt();
        for (const auto& primvar : sceneDelegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationConstant)) {
            if (primvar.name == str::deformKeys) {
                const auto value = sceneDelegate->Get(id, primvar.name);
                if (value.IsHolding<int>()) {
                    extrapolatePoints = SetDeformKeys(value.UncheckedGet<int>());
                }
            } else {
                HdArnoldSetConstantPrimvar(GetArnoldNode(), id, sceneDelegate, primvar);
            }
        }

        // Points, accelerations and velocities are dirtied when HdChange::DirtyPoints is called.
        auto convertToUniformPrimvar = [&](const HdPrimvarDescriptor& primvar) {
            if (primvar.name != HdTokens->points && primvar.name != HdTokens->widths &&
                primvar.name != HdTokens->velocities && primvar.name != HdTokens->accelerations) {
                HdArnoldSetUniformPrimvar(GetArnoldNode(), id, sceneDelegate, primvar);
            }
        };

        for (const auto& primvar : sceneDelegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationUniform)) {
            convertToUniformPrimvar(primvar);
        }

        for (const auto& primvar : sceneDelegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationVertex)) {
            // Per vertex attributes are uniform on points.
            convertToUniformPrimvar(primvar);
        }

        for (const auto& primvar : sceneDelegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationVarying)) {
            // Per vertex attributes are uniform on points.
            convertToUniformPrimvar(primvar);
        }
    }

    if (extrapolatePoints || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        param.Interrupt();
        HdArnoldSetPositionFromPrimvar(GetArnoldNode(), id, sceneDelegate, str::points, param(), GetDeformKeys());
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

PXR_NAMESPACE_CLOSE_SCOPE
