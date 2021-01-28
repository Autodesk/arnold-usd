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
#include "shape.h"

#include "constant_strings.h"
#include "instancer.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

HdArnoldShape::HdArnoldShape(
    const AtString& shapeType, HdArnoldRenderDelegate* delegate, const SdfPath& id, const int32_t primId)
    : _delegate(delegate)
{
    _shape = AiNode(delegate->GetUniverse(), shapeType);
    AiNodeSetStr(_shape, str::name, id.GetText());
    _SetPrimId(primId);
}

HdArnoldShape::~HdArnoldShape()
{
    AiNodeDestroy(_shape);
#ifdef HDARNOLD_USE_INSTANCER
    if (_instancer != nullptr) {
        AiNodeDestroy(_instancer);
    }
#else
    for (auto* instance : _instances) {
        AiNodeDestroy(instance);
    }
#endif
}

void HdArnoldShape::Sync(
    HdRprim* rprim, HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param, bool force)
{
    auto& id = rprim->GetId();
    if (HdChangeTracker::IsPrimIdDirty(dirtyBits, id)) {
        _SetPrimId(rprim->GetPrimId());
    }
    if (dirtyBits | HdChangeTracker::DirtyCategories) {
        _delegate->ApplyLightLinking(_shape, sceneDelegate->GetCategories(id));
    }
    _SyncInstances(dirtyBits, sceneDelegate, param, id, rprim->GetInstancerId(), force);
}

void HdArnoldShape::SetVisibility(uint8_t visibility)
{
    // Either the shape is not instanced or the instances are not yet created. In either case we can set the visibility
    // on the shape.
#ifdef HDARNOLD_USE_INSTANCER
    if (_instancer == nullptr) {
#else
    if (_instances.empty()) {
#endif
        AiNodeSetByte(_shape, str::visibility, visibility);
    }
    _visibility = visibility;
}

void HdArnoldShape::_SetPrimId(int32_t primId)
{
    // Hydra prim IDs are starting from zero, and growing with the number of primitives, so it's safe to directly cast.
    // However, prim ID 0 is valid in hydra (the default value for the id buffer in arnold), so we have to to offset
    // them by one, so we can use the the 0 prim id to detect background pixels reliably both in CPU and GPU backend
    // mode. Later, we'll subtract 1 from the id in the driver.

    AiNodeSetUInt(_shape, str::id, static_cast<unsigned int>(primId) + 1);
}

void HdArnoldShape::_SyncInstances(
    HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param, const SdfPath& id,
    const SdfPath& instancerId, bool force)
{
    // The primitive is not instanced. Instancer IDs are not supposed to be changed during the lifetime of the shape.
    if (instancerId.IsEmpty()) {
        return;
    }
    // TODO(pal) : If the instancer is created without any instances, or it doesn't have any instances, we might end
    //  up with a visible source mesh. We need to investigate if an instancer without any instances is a valid object
    //  in USD. Alternatively, what happens if a prototype is not instanced in USD.
    if (!HdChangeTracker::IsInstancerDirty(dirtyBits, id) && !HdChangeTracker::IsInstanceIndexDirty(dirtyBits, id) &&
        !force) {
        // Visibility still could have changed outside the shape.
#ifdef HDARNOLD_USE_INSTANCER
        _UpdateInstanceVisibility(1, param);
#else
        _UpdateInstanceVisibility(_instances.size(), param);
#endif
        return;
    }
    param->Interrupt();
    // We need to hide the source mesh.
    AiNodeSetByte(_shape, str::visibility, 0);
#ifndef HDARNOLD_USE_INSTANCER
#if 1 // Forcing the re-creation of instances.
    for (auto* instance : _instances) {
        AiNodeDestroy(instance);
    }
    _instances.clear();
#endif
#endif
    auto& renderIndex = sceneDelegate->GetRenderIndex();
    auto* instancer = static_cast<HdArnoldInstancer*>(renderIndex.GetInstancer(instancerId));
    const auto instanceMatrices = instancer->CalculateInstanceMatrices(id);
#ifdef HDARNOLD_USE_INSTANCER
    if (_instancer == nullptr) {
        _instancer = AiNode(_delegate->GetUniverse(), str::instancer);
        std::stringstream ss;
        ss << AiNodeGetName(_shape) << "_instancer";
        AiNodeSetStr(_instancer, str::name, ss.str().c_str());
        AiNodeSetPtr(_instancer, str::nodes, _shape);
        AiNodeDeclare(_instancer, str::instance_inherit_xform, "constant array BOOL");
        AiNodeSetArray(_instancer, str::instance_inherit_xform, AiArray(1, 1, AI_TYPE_BOOLEAN, true));
    }
    const auto instanceCount = static_cast<uint32_t>(instanceMatrices.size());
    auto* matrixArray = AiArrayAllocate(instanceCount, 1, AI_TYPE_MATRIX);
    auto* nodeIdxsArray = AiArrayAllocate(instanceCount, 1, AI_TYPE_UINT);
    auto* matrices = static_cast<AtMatrix*>(AiArrayMap(matrixArray));
    auto* nodeIdxs = static_cast<uint32_t*>(AiArrayMap(nodeIdxsArray));
    for (auto i = decltype(instanceCount){0}; i < instanceCount; i += 1) {
        matrices[i] = HdArnoldConvertMatrix(instanceMatrices[i]);
        nodeIdxs[i] = 0;
    }
    AiArrayUnmap(matrixArray);
    AiArrayUnmap(nodeIdxsArray);
    AiNodeSetArray(_instancer, str::instance_matrix, matrixArray);
    AiNodeSetArray(_instancer, str::node_idxs, nodeIdxsArray);
    AiNodeSetArray(_instancer, str::instance_visibility, AiArray(1, 1, AI_TYPE_BYTE, _visibility));
    instancer->SetPrimvars(_instancer, id, instanceMatrices.size());
#else
    const auto oldSize = _instances.size();
    const auto newSize = instanceMatrices.size();
    for (auto i = newSize; i < oldSize; ++i) {
        AiNodeDestroy(_instances[i]);
    }

    _instances.resize(newSize);
    for (auto i = oldSize; i < newSize; ++i) {
        auto* instance = AiNode(_delegate->GetUniverse(), str::ginstance);
        AiNodeSetByte(instance, str::visibility, _visibility);
        AiNodeSetPtr(instance, str::node, _shape);
        _instances[i] = instance;
        std::stringstream ss;
        ss << AiNodeGetName(_shape) << "_instance_" << i;
        AiNodeSetStr(instance, str::name, ss.str().c_str());
    }
    _UpdateInstanceVisibility(oldSize);
    for (auto i = decltype(newSize){0}; i < newSize; ++i) {
        AiNodeSetMatrix(_instances[i], str::matrix, HdArnoldConvertMatrix(instanceMatrices[i]));
    }
#endif
}

void HdArnoldShape::_UpdateInstanceVisibility(size_t count, HdArnoldRenderParam* param)
{
#ifdef HDARNOLD_USE_INSTANCER
    if (_instancer == nullptr) {
        return;
    }
    auto* instanceVisibility = AiNodeGetArray(_instancer, str::instance_visibility);
    const auto currentVisibility = (instanceVisibility != nullptr && AiArrayGetNumElements(instanceVisibility) == 1)
                                       ? AiArrayGetByte(instanceVisibility, 0)
                                       : ~_visibility;
    if (currentVisibility == _visibility) {
        return;
    }
    if (param != nullptr) {
        param->Interrupt();
    }
    AiNodeSetArray(_instancer, str::instance_visibility, AiArray(1, 1, AI_TYPE_BYTE, _visibility));
#else
    if (count == 0 || _instances.empty()) {
        return;
    }
    // The instance visibilities should be kept in sync all the time, so it's okay to check against the first
    // instance's visibility to see if anything has changed.
    const auto currentVisibility = AiNodeGetByte(_instances.front(), str::visibility);
    // No need to update anything.
    if (currentVisibility == _visibility) {
        return;
    }
    // If param is not nullptr, we have to stop the rendering process and signal that we have to changed something.
    if (param != nullptr) {
        param->Interrupt();
    }
    count = std::min(count, _instances.size());
    for (auto index = decltype(count){0}; index < count; index += 1) {
        AiNodeSetByte(_instances[index], str::visibility, _visibility);
    }
#endif
}

HdDirtyBits HdArnoldShape::GetInitialDirtyBitsMask()
{
    return HdChangeTracker::DirtyInstancer || HdChangeTracker::DirtyInstanceIndex || HdChangeTracker::DirtyCategories ||
           HdChangeTracker::DirtyPrimID;
}

PXR_NAMESPACE_CLOSE_SCOPE
