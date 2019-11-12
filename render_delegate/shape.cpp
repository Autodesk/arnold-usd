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
    for (auto* instance : _instances) {
        AiNodeDestroy(instance);
    }
}

void HdArnoldShape::Sync(
    HdRprim* rprim, HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param)
{
    auto& id = rprim->GetId();
    if (HdChangeTracker::IsPrimIdDirty(dirtyBits, id)) {
        _SetPrimId(rprim->GetPrimId());
    }
    _SyncInstances(dirtyBits, sceneDelegate, param, id, rprim->GetInstancerId());
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
    const SdfPath& instancerId)
{
    if (instancerId.IsEmpty() || !HdChangeTracker::IsInstanceIndexDirty(dirtyBits, id)) {
        return;
    }
    param->End();
    auto& renderIndex = sceneDelegate->GetRenderIndex();
    auto* instancer = static_cast<HdArnoldInstancer*>(renderIndex.GetInstancer(instancerId));
    const auto instanceMatrices = instancer->CalculateInstanceMatrices(id);
    const auto oldSize = _instances.size();
    const auto newSize = instanceMatrices.size();
    for (auto i = newSize; i < oldSize; ++i) {
        AiNodeDestroy(_instances[i]);
    }

    _instances.resize(newSize);
    for (auto i = oldSize; i < newSize; ++i) {
        auto* instance = AiNode(_delegate->GetUniverse(), str::ginstance);
        AiNodeSetPtr(instance, str::node, _shape);
        _instances[i] = instance;
        std::stringstream ss;
        ss << id.GetText() << "_instance_" << i;
        AiNodeSetStr(instance, str::name, ss.str().c_str());
    }

    for (auto i = decltype(newSize){0}; i < newSize; ++i) {
        AiNodeSetMatrix(_instances[i], str::matrix, HdArnoldConvertMatrix(instanceMatrices[i]));
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
