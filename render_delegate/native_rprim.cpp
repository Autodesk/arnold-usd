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
#include "native_rprim.h"

#include "material.h"

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
        for (const auto& paramIt : *_paramList) {
#if PXR_VERSION >= 2011
            const auto val = sceneDelegate->Get(id, str::t_arnold__attributes);
            if (val.IsHolding<ArnoldUsdParamValueList>()) {
                const auto* nodeEntry = AiNodeGetNodeEntry(GetArnoldNode());
                for (const auto& param : val.UncheckedGet<ArnoldUsdParamValueList>()) {
                    HdArnoldSetParameter(GetArnoldNode(), AiNodeEntryLookUpParameter(nodeEntry, param.first), param.second);
                }
            }
#else
            const auto val = sceneDelegate->Get(id, paramIt.first);
            // Do we need to check for this?
            if (!val.IsEmpty()) {
                HdArnoldSetParameter(GetArnoldNode(), paramIt.second, val);
            }
#endif
        }
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        param.Interrupt();
        _UpdateVisibility(sceneDelegate, dirtyBits);
        SetShapeVisibility(_sharedData.visible ? AI_RAY_ALL : uint8_t{0});
    }

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        HdArnoldSetTransform(GetArnoldNode(), sceneDelegate, GetId());
        transformDirtied = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        param.Interrupt();
        const auto* material = reinterpret_cast<const HdArnoldMaterial*>(
            sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, sceneDelegate->GetMaterialId(id)));
        if (material != nullptr) {
            AiNodeSetPtr(GetArnoldNode(), str::shader, material->GetSurfaceShader());
        } else {
            AiNodeResetParameter(GetArnoldNode(), str::shader);
        }
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldNativeRprim::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::InitRepr | HdChangeTracker::DirtyTransform |
           HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyVisibility |
           HdArnoldShape::GetInitialDirtyBitsMask() | ArnoldUsdRprimBitsParams;
}

const TfTokenVector& HdArnoldNativeRprim::GetBuiltinPrimvarNames() const
{
    const static TfTokenVector r{};
    return r;
}

PXR_NAMESPACE_CLOSE_SCOPE
