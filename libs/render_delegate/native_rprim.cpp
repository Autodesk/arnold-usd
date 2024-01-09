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
#include "native_rprim.h"

#include "node_graph.h"

#include <common_bits.h>
#include <constant_strings.h>
#include <shape_utils.h>

PXR_NAMESPACE_OPEN_SCOPE

#if PXR_VERSION >= 2102
HdArnoldNativeRprim::HdArnoldNativeRprim(
    HdArnoldRenderDelegate* renderDelegate, const AtString& arnoldType, const SdfPath& id)
    : HdArnoldRprim<HdRprim>(arnoldType, renderDelegate, id),
      _paramList(renderDelegate->GetNativeRprimParamList(arnoldType))
{
}
#else
HdArnoldNativeRprim::HdArnoldNativeRprim(
    HdArnoldRenderDelegate* renderDelegate, const AtString& arnoldType, const SdfPath& id, const SdfPath& instancerId)
    : HdArnoldRprim<HdRprim>(arnoldType, renderDelegate, id, instancerId),
      _paramList(renderDelegate->GetNativeRprimParamList(arnoldType))
{
}
#endif

void HdArnoldNativeRprim::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    TF_UNUSED(reprToken);
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();

    // Sync any built-in parameters.
    if (*dirtyBits & ArnoldUsdRprimBitsParams && Ai_likely(_paramList != nullptr)) {
        param.Interrupt();
#if PXR_VERSION >= 2011
        const auto val = sceneDelegate->Get(id, str::t_arnold__attributes);
        if (val.IsHolding<ArnoldUsdParamValueList>()) {
            const auto* nodeEntry = AiNodeGetNodeEntry(GetArnoldNode());
            for (const auto& param : val.UncheckedGet<ArnoldUsdParamValueList>()) {
                HdArnoldSetParameter(
                        GetArnoldNode(), AiNodeEntryLookUpParameter(nodeEntry, param.first), 
                        param.second, GetRenderDelegate());
            }
        }
#else
        for (const auto& paramIt : *_paramList) {
            const auto val = sceneDelegate->Get(id, paramIt.first);
            // Do we need to check for this?
            if (!val.IsEmpty()) {
                HdArnoldSetParameter(GetArnoldNode(), paramIt.second, val, GetRenderDelegate());
            }
        }
#endif
    }

    // The last argument means that we don't want to check the sidedness.
    // The doubleSided attribute (off by default)  should not 
    // affect arnold native prims. Visibility should be taken into account though
    CheckVisibilityAndSidedness(sceneDelegate, id, dirtyBits, param, false);

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
            if (AiNodeIs(GetArnoldNode(), str::volume)) {
                AiNodeSetPtr(GetArnoldNode(), str::shader, material->GetVolumeShader());
            } else {
                AiNodeSetPtr(GetArnoldNode(), str::shader, material->GetSurfaceShader());
            }
        } else {
            AiNodeResetParameter(GetArnoldNode(), str::shader);
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        // For arnold native prims, sidedness should default to AI_RAY_ALL, 
        // since that's the default for arnold nodes (as opposed to usd meshes)
        _sidednessFlags.SetHydraFlag(AI_RAY_ALL);
        param.Interrupt();
        for (const auto& primvar : sceneDelegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationConstant)) {
            HdArnoldSetConstantPrimvar(
                GetArnoldNode(), id, sceneDelegate, primvar, &_visibilityFlags, &_sidednessFlags, nullptr, GetRenderDelegate());
        }
        UpdateVisibilityAndSidedness();
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldNativeRprim::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::InitRepr | HdChangeTracker::DirtyTransform |
           HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyVisibility |
           HdChangeTracker::DirtyDoubleSided | HdArnoldShape::GetInitialDirtyBitsMask() | ArnoldUsdRprimBitsParams;
}

const TfTokenVector& HdArnoldNativeRprim::GetBuiltinPrimvarNames() const
{
    const static TfTokenVector r{};
    return r;
}

PXR_NAMESPACE_CLOSE_SCOPE
