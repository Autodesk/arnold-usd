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

namespace {

/// Returns the scene path of a point-instancer prototype copy re-rooted by UsdImaging, whose
/// own path carries a hash suffix, or an empty path for any other prim (and without Hydra 2).
SdfPath _GetPrototypeOriginPath(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
#ifdef ENABLE_SCENE_INDEX // Hydra2
    HdSceneIndexBaseRefPtr sceneIndex = sceneDelegate->GetRenderIndex().GetTerminalSceneIndex();
    if (sceneIndex) {
        HdSceneIndexPrim prim = sceneIndex->GetPrim(id);
        if (HdInstancedBySchema::GetFromParent(prim.dataSource).GetContainer()) {
            HdPrimOriginSchema primOrigin = HdPrimOriginSchema::GetFromParent(prim.dataSource).GetContainer();
            if (primOrigin)
                return primOrigin.GetOriginPath(HdPrimOriginSchemaTokens->scenePath);
        }
    }
#else
    TF_UNUSED(sceneDelegate);
    TF_UNUSED(id);
#endif // ENABLE_SCENE_INDEX // Hydra2
    return {};
}

} // namespace

HdArnoldShape::HdArnoldShape(
    const AtString& shapeType, HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const int32_t primId)
    : _renderDelegate(renderDelegate)
{
    if (!shapeType.empty()) {
        _shape = renderDelegate->CreateArnoldNode(shapeType, AtString(id.GetText()));
        _SetPrimId(primId);
    }
}

HdArnoldShape::~HdArnoldShape()
{
    DestroyNodes();
}

void HdArnoldShape::SetShapeType(const AtString& shapeType, const SdfPath& id, int32_t primId, bool recreate)
{
    if (_shape != nullptr && (recreate || !AiNodeIs(_shape, shapeType))) {
        _renderDelegate->DestroyArnoldNode(_shape);
        _shape = nullptr;
    }
    if (_shape == nullptr) {
        _shape = _renderDelegate->CreateArnoldNode(shapeType, AtString(id.GetText()));
        _sharedGeometry = nullptr;
        _hiddenBy = HiddenBy::None;
        // Sync only applies the prim id when it is dirty.
        _SetPrimId(primId);
    }
}

void HdArnoldShape::ReleaseShapeOwnership()
{
    _shape = nullptr;
    _sharedGeometry = nullptr;
}

void HdArnoldShape::DestroyNodes()
{
    DestroyInstancers();
    _renderDelegate->DestroyArnoldNode(_shape);
    ReleaseShapeOwnership();
}

bool HdArnoldShape::DestroyInstancers()
{
    if (_instancers.empty())
        return false;
    for (auto& instancer : _instancers) {
        _renderDelegate->DestroyArnoldNode(instancer);
    }
    _instancers.clear();
    return true;
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
    // Identify if this rprim comes from a prototype in a point instancer,
    // then set the metadata to override it's cryptomatte id with the
    // prototype path minus the hash suffix
    const SdfPath primOriginPath = _GetPrototypeOriginPath(sceneDelegate, id);
    if (!primOriginPath.IsEmpty()) {
        param.Interrupt();
        if (AiNodeLookUpUserParameter(_shape, str::crypto_object) == nullptr) {
            AiNodeDeclare(_shape, str::crypto_object, str::constantString);
        }
        AiNodeSetStr(_shape, str::crypto_object, AtString(primOriginPath.GetText()));
    }

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

    if (_sharedGeometry != nullptr && rprim->GetInstancerId().IsEmpty()) {
        _SyncSharedGeometryInstancer(dirtyBits, sceneDelegate, param, id, rprim->GetPrimId(), primOriginPath);
        return;
    }
    _SyncInstances(
        dirtyBits, _renderDelegate, sceneDelegate, param, id, rprim->GetInstancerId(), force, rprim->GetPrimId(),
        primOriginPath);
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

void HdArnoldShape::SetHidden(bool hidden, bool nodeIsShared)
{
    if (_shape == nullptr)
        return;
    if (hidden) {
        if (!_instancers.empty()) {
            for (AtNode* instancer : _instancers)
                AiNodeSetDisabled(instancer, true);
            _hiddenBy = HiddenBy::Instancers;
        } else if (nodeIsShared) {
            AiNodeSetByte(_shape, str::visibility, 0);
            _hiddenBy = HiddenBy::Visibility;
        } else {
            AiNodeSetDisabled(_shape, true);
            _hiddenBy = HiddenBy::Disabled;
        }
        return;
    }
    switch (_hiddenBy) {
        case HiddenBy::Instancers:
            for (AtNode* instancer : _instancers)
                AiNodeSetDisabled(instancer, false);
            break;
        case HiddenBy::Visibility:
            AiNodeSetByte(_shape, str::visibility, _instancers.empty() ? _visibility : 0);
            break;
        case HiddenBy::Disabled:
            AiNodeSetDisabled(_shape, false);
            break;
        case HiddenBy::None:
            // Hidden by the render tags, which the caller already checked now allow it.
            AiNodeSetDisabled(_shape, false);
            for (AtNode* instancer : _instancers)
                AiNodeSetDisabled(instancer, false);
            break;
    }
    _hiddenBy = HiddenBy::None;
}

bool HdArnoldShape::IsHidden() const
{
    if (_shape == nullptr)
        return false;
    if (_hiddenBy != HiddenBy::None)
        return true;
    // Not hidden by us, but the render tags can still have disabled what renders.
    return AiNodeIsDisabled(_instancers.empty() ? _shape : _instancers.front());
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
    HdArnoldRenderParamInterrupt& param, const SdfPath& id, const SdfPath& instancerId, bool force,
    int32_t primId, const SdfPath& cryptoObject)
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
    // Changes of the instance categories are only notified to the rprims as DirtyInstancer.
    const bool instancesChanged = HdChangeTracker::IsInstancerDirty(dirtyBits, id) ||
                                  HdChangeTracker::IsInstanceIndexDirty(dirtyBits, id) || force;
    const bool instancesDirty = instancesChanged || HdChangeTracker::IsPrimvarDirty(dirtyBits, id, HdTokens->points);
    const bool categoriesDirty = (dirtyBits & HdChangeTracker::DirtyCategories) != 0;
    if (!instancesDirty && !categoriesDirty) {
        // Visibility still could have changed outside the shape.
        _UpdateInstanceVisibility(param);
        return;
    }

    // The instance light-link categories can only change along with the instances or the categories,
    // they are kept for the other rebuilds of the instancers (e.g. when the points change).
    bool categoriesChanged = false;
    if (instancesChanged || categoriesDirty) {
        const bool hasLightLinking = renderDelegate->HasLightLinking();
        std::vector<VtArray<TfToken>> instanceCategories;
        if (hasLightLinking) {
            if (auto* arnoldInstancer = dynamic_cast<HdArnoldInstancer*>(instancer))
                instanceCategories = arnoldInstancer->ComputeInstanceCategories(id);
        }
        categoriesChanged =
            categoriesDirty || hasLightLinking != _hasLightLinking || instanceCategories != _instanceCategories;
        _hasLightLinking = hasLightLinking;
        _instanceCategories = std::move(instanceCategories);
        // The rprims leave the light linking of instanced shapes to us, see ApplyLightLinking.
        if (categoriesChanged) {
            param.Interrupt();
            renderDelegate->ApplyLightLinking(sceneDelegate, _shape, id, _instanceCategories);
        }
    }

    // Instances that don't all share the same light-link categories need per-instance light
    // groups, which only an Arnold instancer node can carry. When they share them, they are
    // applied to the shape itself by ApplyLightLinking.
    const bool perInstanceLightLinking = _instanceCategories.size() > 1;
    // Categories to set per instance on the leaf instancer node. A shared geometry carries the
    // light groups of another rprim, so they are overridden even when the instances share them.
    auto getLeafInstanceCategories = [&]() -> std::vector<VtArray<TfToken>> {
        if (perInstanceLightLinking)
            return _instanceCategories;
        if (_sharedGeometry != nullptr && _hasLightLinking)
            return _instanceCategories.empty() ? std::vector<VtArray<TfToken>>(1) : _instanceCategories;
        return {};
    };

    if (!instancesDirty && perInstanceLightLinking == _perInstanceLightLinking) {
        // Only the light-link categories changed, update the existing instancer.
        if (categoriesChanged && !_instancers.empty()) {
            param.Interrupt();
            renderDelegate->ApplyInstanceLightLinking(sceneDelegate, _instancers[0], id, getLeafInstanceCategories());
        }
        // Visibility still could have changed outside the shape.
        _UpdateInstanceVisibility(param);
        return;
    }
    _perInstanceLightLinking = perInstanceLightLinking;

    // Rebuild the instancer
    param.Interrupt();

    // Only an instancer node can reference a shared geometry (geometry deduplication).
    if (_sharedGeometry != nullptr || _forceInstancerNode || _perInstanceLightLinking ||
        UseArnoldInstancer(sceneDelegate, _renderDelegate, instancer, _shape)) {
        // First destroy the arnold parent instancers to this mesh
        DestroyInstancers();

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

        AtNode* const leafPrototype = (_sharedGeometry != nullptr) ? _sharedGeometry : _shape;
        // A shared geometry carries another rprim's prim id and cryptomatte object name:
        // override them per instance on the leaf instancer (a single value applies to all).
        if (_sharedGeometry != nullptr && !_instancers.empty()) {
            AtNode* const leafInstancer = _instancers[0];
            if (AiNodeLookUpUserParameter(leafInstancer, str::instance_hydraPrimId) == nullptr)
                AiNodeDeclare(leafInstancer, str::instance_hydraPrimId, str::constantArrayInt);
            AiNodeSetArray(leafInstancer, str::instance_hydraPrimId, AiArray(1, 1, AI_TYPE_INT, primId + 1));
            if (!cryptoObject.IsEmpty()) {
                if (AiNodeLookUpUserParameter(leafInstancer, str::instance_crypto_object) == nullptr)
                    AiNodeDeclare(leafInstancer, str::instance_crypto_object, str::constantArrayString);
                AtArray* cryptoArray = AiArrayAllocate(1, 1, AI_TYPE_STRING);
                AiArraySetStr(cryptoArray, 0, AtString(cryptoObject.GetText()));
                AiNodeSetArray(leafInstancer, str::instance_crypto_object, cryptoArray);
            }
        }
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
        // Only the leaf instancer creates the instances of the shape, see ComputeInstanceCategories.
        if (!_instancers.empty()) {
            const std::vector<VtArray<TfToken>> leafInstanceCategories = getLeafInstanceCategories();
            if (!leafInstanceCategories.empty())
                renderDelegate->ApplyInstanceLightLinking(sceneDelegate, _instancers[0], id, leafInstanceCategories);
        }
    } else
    {
        // The geometry deduplication can switch a shape from the instancer-node path to this
        // one: drop the previous instancers, and show the shape again which they had hidden.
        if (DestroyInstancers())
            AiNodeSetByte(_shape, str::visibility, _visibility);

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

void HdArnoldShape::_SyncSharedGeometryInstancer(
    HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParamInterrupt& param, const SdfPath& id,
    int32_t primId, const SdfPath& cryptoObject)
{
    // What can change the state overridden on the instance. The instancer is destroyed by the
    // geometry deduplication when the canonical changes (see HdArnoldRprim::_ApplyGeometryDedup).
    constexpr HdDirtyBits instanceDirtyBits =
        HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility | HdChangeTracker::DirtyDoubleSided |
        HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtyCategories | HdChangeTracker::DirtyPrimvar |
        HdChangeTracker::DirtyPrimID | HdChangeTracker::DirtyInstancer;
    if (!_instancers.empty() && (dirtyBits & instanceDirtyBits) == 0)
        return;
    param.Interrupt();
    if (_instancers.empty()) {
        // The instancer is kept and updated in place afterwards: a node can't always be destroyed
        // while the procedural is expanded, which would leave the name of a new one taken.
        AtNode* instancer = _renderDelegate->CreateArnoldNode(
            str::instancer, AtString((id.GetString() + "/shared_geometry_instancer").c_str()));
        _instancers.push_back(instancer);
        AiNodeSetPtr(instancer, str::nodes, _sharedGeometry);
        if (AiNodeLookUpUserParameter(instancer, str::instance_inherit_xform) == nullptr)
            AiNodeDeclare(instancer, str::instance_inherit_xform, str::constantArrayBool);
        AiNodeSetArray(instancer, str::instance_inherit_xform, AiArray(1, 1, AI_TYPE_BOOLEAN, false));
        _renderDelegate->TrackRenderTag(instancer, sceneDelegate->GetRenderTag(id));
        // The own node only carries the state of the rprim.
        AiNodeSetByte(_shape, str::visibility, 0);
    }
    AtNode* const instancer = _instancers.front();
    auto setUserArray = [&](const AtString& name, const AtString& declaration, AtArray* value) {
        if (AiNodeLookUpUserParameter(instancer, name) == nullptr)
            AiNodeDeclare(instancer, name, declaration);
        AiNodeSetArray(instancer, name, value);
    };
    auto resetUserArray = [&](const AtString& name) {
        if (AiNodeLookUpUserParameter(instancer, name) != nullptr)
            AiNodeResetParameter(instancer, name);
    };

    // The instancer reinitializes, and so rebuilds its instance, when its matrices are set. It
    // doesn't for all the other parameters, so they are always set along with the matrices.
    const AtArray* matrices = AiNodeGetArray(_shape, str::matrix);
    AiNodeSetArray(
        instancer, str::instance_matrix,
        matrices != nullptr ? AiArrayCopy(matrices) : AiArray(1, 1, AI_TYPE_MATRIX, AiM4Identity()));
    // Without transform keys, the instance keeps the motion range of the canonical's deformation.
    if (matrices != nullptr && AiArrayGetNumKeys(matrices) > 1) {
        setUserArray(
            str::instance_motion_start, str::constantArrayFloat,
            AiArray(1, 1, AI_TYPE_FLOAT, AiNodeGetFlt(_shape, str::motion_start)));
        setUserArray(
            str::instance_motion_end, str::constantArrayFloat,
            AiArray(1, 1, AI_TYPE_FLOAT, AiNodeGetFlt(_shape, str::motion_end)));
    } else {
        resetUserArray(str::instance_motion_start);
        resetUserArray(str::instance_motion_end);
    }
    AiNodeSetArray(instancer, str::instance_visibility, AiArray(1, 1, AI_TYPE_BYTE, _visibility));
    setUserArray(
        str::instance_sidedness, str::constantArrayByte,
        AiArray(1, 1, AI_TYPE_BYTE, AiNodeGetByte(_shape, str::sidedness)));
    // A duplicate has no geometry subsets, so a single surface shader.
    const AtArray* shaders = AiNodeGetArray(_shape, str::shader);
    if (shaders != nullptr && AiArrayGetNumElements(shaders) > 0)
        AiNodeSetArray(instancer, str::instance_shader, AiArray(1, 1, AI_TYPE_NODE, AiArrayGetPtr(shaders, 0)));
    else
        AiNodeResetParameter(instancer, str::instance_shader);
    // The prim id and cryptomatte object name of the canonical would apply otherwise. The
    // instance is named after the instancer, so the object name defaults to the prim path.
    setUserArray(str::instance_hydraPrimId, str::constantArrayInt, AiArray(1, 1, AI_TYPE_INT, primId + 1));
    AtArray* cryptoArray = AiArrayAllocate(1, 1, AI_TYPE_STRING);
    AiArraySetStr(cryptoArray, 0, AtString(cryptoObject.IsEmpty() ? id.GetText() : cryptoObject.GetText()));
    setUserArray(str::instance_crypto_object, str::constantArrayString, cryptoArray);
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
