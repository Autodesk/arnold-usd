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
#include <pxr/imaging/hd/retainedDataSource.h>
PXR_NAMESPACE_OPEN_SCOPE

HdArnoldNativeRprim::HdArnoldNativeRprim(
    HdArnoldRenderDelegate* renderDelegate, const AtString& arnoldType, const SdfPath& id)
    : HdArnoldRprim<HdRprim>(arnoldType, renderDelegate, id),
      _paramList(renderDelegate->GetNativeRprimParamList(arnoldType))
{
}

void HdArnoldNativeRprim::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    if (!GetRenderDelegate()->CanUpdateScene())
        return;
 
    TF_UNUSED(reprToken);
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();

    if (*dirtyBits & HdChangeTracker::DirtyCategories) {
        param.Interrupt();
        _renderDelegate->ApplyLightLinking(sceneDelegate, GetArnoldNode(), GetId());
    }

    auto GetArnoldAttributes = [&]() {
        // Try to get the arnold attributes via the scene delegate
        const auto val = sceneDelegate->Get(id, str::t_arnold__attributes);
        if (!val.IsEmpty())
            return val;
#ifdef ENABLE_SCENE_INDEX // Hydra2
        // Otherwise try with the sceneIndex
        HdSceneIndexBaseRefPtr sceneIndex = sceneDelegate->GetRenderIndex().GetTerminalSceneIndex();
        if (sceneIndex) {
            HdSampledDataSourceHandle arnoldAttribute = HdSampledDataSource::Cast(
                sceneIndex->GetDataSource(id, HdDataSourceLocator(str::t_arnold__attributes)));
            if (arnoldAttribute) {
                return arnoldAttribute->GetValue(0.0);
            }   
        }
#endif // ENABLE_SCENE_INDEX // Hydra2
        return VtValue();
    };

    // If the primitive is invisible for hydra, we want to skip it here
    if (SkipHiddenPrim(sceneDelegate, id, dirtyBits, param))
        return;

    int defaultVisibility = AI_RAY_ALL;
    // Sync any built-in parameters.
    bool syncBuiltinParameters = *dirtyBits & ArnoldUsdRprimBitsParams;
 #ifdef ENABLE_SCENE_INDEX // Hydra2 - we have to pass the arnold::attributes via the DirtyPrimvar bit unfortunately,
                         // the mapping is done in fixInvalidationSIP.cpp
    syncBuiltinParameters = syncBuiltinParameters || (*dirtyBits & HdChangeTracker::DirtyPrimvar);
 #endif   
    if (syncBuiltinParameters && Ai_likely(_paramList != nullptr)) {
        param.Interrupt();
        VtValue val = GetArnoldAttributes();
        if (val.IsHolding<ArnoldUsdParamValueList>()) {
            const auto* nodeEntry = AiNodeGetNodeEntry(GetArnoldNode());
            for (const auto& param : val.UncheckedGet<ArnoldUsdParamValueList>()) {
                HdArnoldSetParameter(
                    GetArnoldNode(), AiNodeEntryLookUpParameter(nodeEntry, param.first), param.second,
                    GetRenderDelegate());
                if (param.first == str::t_visibility)
                    defaultVisibility = (int)AiNodeGetByte(GetArnoldNode(), str::visibility);
            }
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        param.Interrupt();
        const auto materialId = sceneDelegate->GetMaterialId(id);
        // Ensure the reference from this shape to its material is properly tracked
        // by the render delegate
        GetRenderDelegate()->TrackDependencies(id, HdArnoldRenderDelegate::PathSetWithDirtyBits {{materialId, HdChangeTracker::DirtyMaterialId}});
        const auto* material = HdArnoldNodeGraph::GetNodeGraph(sceneDelegate->GetRenderIndex(), materialId);
        if (material != nullptr) {
            if (AiNodeIs(GetArnoldNode(), str::volume)) {
                AiNodeSetPtr(GetArnoldNode(), str::shader, material->GetCachedVolumeShader());
            } else {
                AiNodeSetPtr(GetArnoldNode(), str::shader, material->GetCachedSurfaceShader());
            }
        } else {
            AiNodeResetParameter(GetArnoldNode(), str::shader);
        }
    }
    AtNode* node = GetArnoldNode();
    CheckVisibilityAndSidedness(sceneDelegate, id, dirtyBits, param, false);

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar || 
        *dirtyBits & HdChangeTracker::DirtyVisibility) {

        _visibilityFlags.ClearPrimvarFlags();
        _visibilityFlags.SetHydraFlag(_sharedData.visible ? AI_RAY_ALL : 0);
        if (defaultVisibility != AI_RAY_ALL) {
            _visibilityFlags.SetPrimvarFlag(AI_RAY_ALL, false);
            _visibilityFlags.SetPrimvarFlag(defaultVisibility, true);
        }

        HdArnoldPrimvarMap primvars;
        std::vector<HdInterpolation> interpolations = {HdInterpolationConstant};
        HdArnoldGetPrimvars(sceneDelegate, GetId(), *dirtyBits, primvars, &interpolations);
        
        param.Interrupt();
        
        for (const auto &p : primvars) {
            if (ConvertPrimvarToBuiltinParameter(node, p.first, p.second.value, &_visibilityFlags, nullptr, nullptr, _renderDelegate))
                continue;

            // Get the parameter name, removing the arnold:prefix if any
            std::string paramName(TfStringStartsWith(p.first.GetString(), str::arnold) ? p.first.GetString().substr(7) : p.first.GetString());
            HdArnoldSetConstantPrimvar(node, TfToken(paramName), p.second.role, p.second.value, &_visibilityFlags,
                    nullptr, nullptr, _renderDelegate);
        }
        
        const auto visibility = _visibilityFlags.Compose();
        AiNodeSetByte(node, str::visibility, visibility);
    }
    
    // NOTE: HdArnoldSetTransform must be set after the primvars as, at the moment, we might rewrite the transform in the
    // primvars and it doesn't take into account the inheritance.
    bool transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, GetId())) {
        param.Interrupt();
        HdArnoldRenderParam* arnoldRenderParam = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
        HdArnoldSetTransform(node, sceneDelegate, GetId(), arnoldRenderParam->GetShutterRange());
        transformDirtied = true;
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

// TODO conversion between HdDirtyBits and ArnoldUsdRprimBitsParams

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
