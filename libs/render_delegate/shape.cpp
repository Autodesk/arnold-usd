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
#include "shape.h"
#include <pxr/base/trace/trace.h>

#ifdef ENABLE_SCENE_INDEX // Hydra2
#include <pxr/imaging/hd/primOriginSchema.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#endif // ENABLE_SCENE_INDEX // Hydra2

#include <constant_strings.h>
#include "instancer.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

HdArnoldShape::HdArnoldShape(
    const AtString& shapeType, HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const int32_t primId)
    : _renderDelegate(renderDelegate)
{
    if (!shapeType.empty()) {
        _shape = renderDelegate->CreateArnoldNode(shapeType, AtString(id.GetText()));
        _isGinstance = shapeType == str::ginstance;
        _SetPrimId(primId);
    }
}

HdArnoldShape::~HdArnoldShape()
{
    if (_shape) {
        _renderDelegate->DestroyArnoldNode(_shape);
    }
    for (auto &instancer : _instancers) {
        _renderDelegate->DestroyArnoldNode(instancer);
    }
}

void HdArnoldShape::SetShapeType(const AtString& shapeType, const SdfPath& id, int32_t primId)
{
    // An initialized ginstance can never be reused, and AiNodeIs must not be trusted on one:
    // during its node_initialize a ginstance mutates itself into a copy of its prototype
    // (copyFromNode assigns the prototype's node entry), so after the first render it answers
    // AiNodeIs(node, "polymesh") == true, it no longer exposes the "node" parameter it was
    // pointed at, and it has no node_update at all (MsgUnreachableCode - arnold assumes no
    // ginstance survives to update time, which interactive editing breaks). Asking arnold here
    // therefore turned both "turn this instance back into real geometry" and "point it at
    // another prototype" into silent no-ops, leaving the rprim believing it owned geometry
    // while its node still aliased the prototype's vlist/vidxs/nsides (ARNOLD-17180). So we
    // always recreate when either side is a ginstance; no other node type morphs, which keeps
    // AiNodeIs the right test for e.g. ArnoldProceduralCustom changing its node entry.
    if (_shape != nullptr && (_isGinstance || !AiNodeIs(_shape, shapeType))) {
        _renderDelegate->DestroyArnoldNode(_shape);
        _shape = nullptr;
    }
    if (_shape == nullptr) {
        _shape = _renderDelegate->CreateArnoldNode(shapeType, AtString(id.GetText()));
        _isGinstance = shapeType == str::ginstance;
        // A brand new node carries none of the previous one's state, and the hydra prim ID is
        // not re-applied by Sync unless DirtyPrimID happens to be set - which it is not on the
        // dedup conversions, nor when ArnoldProceduralCustom swaps its node entry. Without
        // this the node has no hydra_primId user data at all, the primId AOV reads 0 for it,
        // and the driver treats those pixels as background: the prim becomes unpickable.
        _SetPrimId(primId);
    }
}

void HdArnoldShape::ConvertToInstanceOf(AtNode* proto, const SdfPath& id, int32_t primId)
{
    if (proto == nullptr)
        return;
    SetShapeType(str::ginstance, id, primId);
    if (_shape == nullptr)
        return;
    // The instance positions itself with its own matrix (set later during Sync), so it
    // must not inherit the prototype transform.
    AiNodeSetBool(_shape, str::inherit_xform, false);
    AiNodeSetPtr(_shape, str::node, proto);
}

AtNode* HdArnoldShape::ReleaseShapeOwnership()
{
    AtNode* node = _shape;
    _shape = nullptr;
    _isGinstance = false;
    return node;
}

void HdArnoldShape::Sync(
    HdRprim* rprim, HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParamInterrupt& param,
    bool force)
{
    AiProfileBlock("hydra_proc:HdArnoldShape:Sync");
    TRACE_FUNCTION();
    if (_shape == nullptr)
        return;

    auto& id = rprim->GetId();
#ifdef ENABLE_SCENE_INDEX // Hydra2
    HdSceneIndexBaseRefPtr sceneIndex = sceneDelegate->GetRenderIndex().GetTerminalSceneIndex();
    if (sceneIndex) {
        // Identify if this rprim comes from a prototype in a point instancer,
        // then set the metadata to override it's cryptomatte id with the
        // prototype path minus the hash suffix
        HdSceneIndexPrim prim = sceneIndex->GetPrim(id);
        HdInstancedBySchema instancedBy = HdInstancedBySchema::GetFromParent(prim.dataSource).GetContainer();
        if (instancedBy) {
            HdPrimOriginSchema primOrigin = HdPrimOriginSchema::GetFromParent(prim.dataSource).GetContainer();
            if (primOrigin) {
                const SdfPath primOriginPath = primOrigin.GetOriginPath(HdPrimOriginSchemaTokens->scenePath);

                param.Interrupt();

                if (AiNodeLookUpUserParameter(_shape, AtString("crypto_object")) == nullptr) {
                    AiNodeDeclare(_shape, AtString("crypto_object"), AtString("constant STRING"));
                }
                AiNodeSetStr(_shape, AtString("crypto_object"), primOriginPath.GetText());
            }
        }
    }
#endif // ENABLE_SCENE_INDEX // Hydra2

    if (HdChangeTracker::IsPrimIdDirty(dirtyBits, id)) {
        param.Interrupt();
        _SetPrimId(rprim->GetPrimId());
    }
#if PXR_VERSION < 2408
    // If render tags are empty, we are displaying everything.
     if (dirtyBits & HdChangeTracker::DirtyRenderTag) {
         param.Interrupt();
         const auto renderTag = sceneDelegate->GetRenderTag(id);
         _renderDelegate->TrackRenderTag(_shape, renderTag);
         for (auto &instancer : _instancers) {
             _renderDelegate->TrackRenderTag(instancer, renderTag);
         }
     }
#endif

    _SyncInstances(dirtyBits, _renderDelegate, sceneDelegate, param, id, rprim->GetInstancerId(), force);
}

void HdArnoldShape::UpdateRenderTag(HdRprim* rprim, HdSceneDelegate *sceneDelegate, HdArnoldRenderParamInterrupt& param){
    param.Interrupt();
    const auto renderTag = sceneDelegate->GetRenderTag(rprim->GetId());
    _renderDelegate->TrackRenderTag(_shape, renderTag);
    for (auto &instancer : _instancers) {
        _renderDelegate->TrackRenderTag(instancer, renderTag);
    }
}

void HdArnoldShape::SetVisibility(uint8_t visibility)
{
    if (_shape == nullptr)
        return;
    // Either the shape is not instanced or the instances are not yet created. In either case we can set the visibility
    // on the shape.
    if (_instancers.empty()) {
        AiNodeSetByte(_shape, str::visibility, visibility);
    }
    _visibility = visibility;
}

static bool UseArnoldInstancer(HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate *renderDelegate, HdInstancer *instancer, AtNode *node)
{
    if (!renderDelegate->SupportShapeInstancing())
        return true;

    // If we have a nested instancer configuration, we'll use an arnold instancer node.
    // Nested instances are handled by a chain of arnold instancer nodes
    // (see HdArnoldInstancer::CreateArnoldInstancer) rather than being flattened into
    // shape instancing. Unless HDARNOLD_FLATTEN_INSTANCING is enabled, in which case we
    // skip this test and flatten the nested instances into shape instancing.
    if (!renderDelegate->FlattenInstancing()) {
        HdInstancer* parentInstancer = sceneDelegate->GetRenderIndex().GetInstancer(instancer->GetParentId());
        if (parentInstancer)
            return true;
    }

    // Procedural nodes do not currently support shapes inner instancing
    return AiNodeEntryGetDerivedType(AiNodeGetNodeEntry(node)) == AI_NODE_SHAPE_PROCEDURAL;
}
void HdArnoldShape::_SetPrimId(int32_t primId)
{
    if (_shape == nullptr)
        return;
    // Hydra prim IDs are starting from zero, and growing with the number of primitives, so it's safe to directly cast.
    // However, prim ID 0 is valid in hydra (the default value for the id buffer in arnold), so we have to to offset
    // them by one, so we can use the 0 prim id to detect background pixels reliably both in CPU and GPU backend
    // mode. Later, we'll subtract 1 from the id in the driver.

    // We are skipping declaring the parameter, since it's causing a crash in the core.
    if (AiNodeLookUpUserParameter(_shape, str::hydraPrimId) == nullptr) {
        AiNodeDeclare(_shape, str::hydraPrimId, str::constantInt);
    }
    AiNodeSetInt(_shape, str::hydraPrimId, primId + 1);
}

void HdArnoldShape::_SyncInstances(
    HdDirtyBits dirtyBits, HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate,
    HdArnoldRenderParamInterrupt& param, const SdfPath& id, const SdfPath& instancerId, bool force)
{
    if (_shape == nullptr)
        return;
    // The primitive is not instanced. Instancer IDs are not supposed to be changed during the lifetime of the shape.
    if (instancerId.IsEmpty()) {
        return;
    }
    HdInstancer * instancer = sceneDelegate->GetRenderIndex().GetInstancer(instancerId);
    if (!instancer)
        return;

    // TODO(pal) : If the instancer is created without any instances, or it doesn't have any instances, we might end
    //  up with a visible source mesh. We need to investigate if an instancer without any instances is a valid object
    //  in USD. Alternatively, what happens if a prototype is not instanced in USD.
    if (!HdChangeTracker::IsPrimvarDirty(dirtyBits, id, HdTokens->points) 
    && !HdChangeTracker::IsInstancerDirty(dirtyBits, id) 
    && !HdChangeTracker::IsInstanceIndexDirty(dirtyBits, id) 
    && !force) {
        // Visibility still could have changed outside the shape.
        _UpdateInstanceVisibility(param);
        return;
    }

    // Rebuild the instancer
    param.Interrupt();

    // A deduplicated prototype (mesh dedup) references a shared canonical polymesh instead
    // of its own geometry; that requires the arnold instancer-node path (which lets us
    // redirect the referenced node), so we force it whenever an override is set. Likewise a
    // prototype that may itself be shared as a canonical must not bake instance_matrix onto
    // its polymesh (shape-instancing), so _forceInstancerNode keeps it a plain shareable node.
    if (_prototypeOverride != nullptr || _forceInstancerNode ||
        UseArnoldInstancer(sceneDelegate, _renderDelegate, instancer, _shape)) {
        // First destroy the arnold parent instancers to this mesh
        for (auto &instancerNode : _instancers) {
            _renderDelegate->DestroyArnoldNode(instancerNode);
        }
        _instancers.clear();

        // We need to hide the source mesh.
        AiNodeSetByte(_shape, str::visibility, 0);

        // Get the hydra instancer and rebuild the arnold instancer.
        // GetInstancer can return a non-HdArnoldInstancer or null, so use dynamic_cast and bail safely.
        auto& renderIndex = sceneDelegate->GetRenderIndex();
        auto* hydraInstancer = dynamic_cast<HdArnoldInstancer*>(renderIndex.GetInstancer(instancerId));
        if (hydraInstancer == nullptr)
            return;
        hydraInstancer->CreateArnoldInstancer(renderDelegate, id, _instancers);

        const TfToken renderTag = sceneDelegate->GetRenderTag(id);

        // For a deduplicated prototype the leaf instancer references the shared canonical
        // polymesh rather than this shape's own (empty) node.
        AtNode* const leafPrototype = (_prototypeOverride != nullptr) ? _prototypeOverride : _shape;
        for (size_t i = 0; i < _instancers.size(); ++i) {
            AiNodeSetPtr(_instancers[i], str::nodes, (i == 0) ? leafPrototype : _instancers[i - 1]);
            renderDelegate->TrackRenderTag(_instancers[i], renderTag);

            // At this point the instancers might have set their instance visibilities.
            // In this case we want to apply the proto shape visibility on top of it. 
            // Otherwise we just set the shape visibility as its instance_visibility
            AtArray *instanceVisibility = AiNodeGetArray(_instancers[i], str::instance_visibility);
            unsigned int instanceVisibilityCount = (instanceVisibility) ? AiArrayGetNumElements(instanceVisibility) : 0;
            if (instanceVisibilityCount  > 0) {
                unsigned char* instVisArray = static_cast<unsigned char*>(AiArrayMap(instanceVisibility));
                for (unsigned int j = 0; j < instanceVisibilityCount; ++j) {
                    instVisArray[j] &= _visibility;
                }
                AiArrayUnmap(instanceVisibility);
                AiNodeSetArray(_instancers[i], str::instance_visibility, instanceVisibility);
            } else
                AiNodeSetArray(_instancers[i], str::instance_visibility, AiArray(1, 1, AI_TYPE_BYTE, _visibility));
        }
    } else
    {
        // Shape-instancing path: instance_matrix is baked onto _shape and no arnold instancer
        // node is involved. Any instancer built by a previous Sync has to go, otherwise it
        // keeps rendering a second, stale copy of the instances - and its "nodes" may point at
        // a node that has since been destroyed. This condition is not static: the mesh dedup
        // flips _forceInstancerNode (and _prototypeOverride) as a prototype becomes eligible
        // or ineligible, so a shape can genuinely move from the branch above to this one.
        const bool hadInstancers = !_instancers.empty();
        for (auto &instancerNode : _instancers) {
            _renderDelegate->DestroyArnoldNode(instancerNode);
        }
        _instancers.clear();
        // That instancer had hidden the source mesh (visibility 0 above). This branch renders
        // the instances on _shape itself, so give it its visibility back - SetVisibility skips
        // the shape while _instancers is non-empty, so nothing else would restore it until the
        // next edit that happens to dirty visibility.
        if (hadInstancers) {
            AiNodeSetByte(_shape, str::visibility, _visibility);
        }

        auto& renderIndex = sceneDelegate->GetRenderIndex();
        // GetInstancer can return a non-HdArnoldInstancer or null, so use dynamic_cast and bail safely.
        auto* instancer = dynamic_cast<HdArnoldInstancer*>(renderIndex.GetInstancer(instancerId));
        if (instancer == nullptr)
            return;
        if (instancer->ComputeShapeInstancesTransforms(_renderDelegate, id, _shape)) {
            instancer->ApplyInstancerVisibilityToArnoldNode(_shape);
        } else {
            // hide the source mesh if it doesn't have any instance #2557
            AiNodeSetByte(_shape, str::visibility, 0);
        }
    }
}

void HdArnoldShape::_UpdateInstanceVisibility(HdArnoldRenderParamInterrupt& param)
{
    if (_instancers.empty())
        return;

    for (auto &instancer : _instancers) {
        AtArray* instanceVisibility = AiNodeGetArray(instancer, str::instance_visibility);
        unsigned int instVisibilityCount = (instanceVisibility) ? AiArrayGetNumElements(instanceVisibility) : 0;
        
        if (instVisibilityCount == 0) {
            AtArray *visArray = AiNodeGetArray(instancer, str::instance_visibility);
            if (visArray == nullptr || AiArrayGetNumElements(visArray) != 1 || AiArrayGetByte(visArray, 0) != _visibility) {
                param.Interrupt();
                AiNodeSetArray(instancer, str::instance_visibility, AiArray(1, 1, AI_TYPE_BYTE, _visibility));
            }            
        } else {
            bool changed = false;
            unsigned char* instVisArray = static_cast<unsigned char*>(AiArrayMap(instanceVisibility));
            for (unsigned int j = 0; j < instVisibilityCount; ++j) {
                unsigned char oldVis = instVisArray[j];
                instVisArray[j] &= _visibility;
                if (oldVis != instVisArray[j])
                    changed = true;
            }
            AiArrayUnmap(instanceVisibility);
            if (changed)
            {
                param.Interrupt();
                AiNodeSetArray(instancer, str::instance_visibility, instanceVisibility);
            }
        }
    }
}


PXR_NAMESPACE_CLOSE_SCOPE
