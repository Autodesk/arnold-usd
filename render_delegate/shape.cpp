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

#include <constant_strings.h>
#include "instancer.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

HdArnoldShape::HdArnoldShape(
    const AtString& shapeType, HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const int32_t primId)
{
    _shape = AiNode(renderDelegate->GetUniverse(), shapeType);
    AiNodeSetStr(_shape, str::name, AtString(id.GetText()));
    _SetPrimId(primId);
}

HdArnoldShape::~HdArnoldShape()
{
    AiNodeDestroy(_shape);
    if (_instancer != nullptr) {
        AiNodeDestroy(_instancer);
    }
}

void HdArnoldShape::Sync(
    HdRprim* rprim, HdDirtyBits dirtyBits, HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate,
    HdArnoldRenderParamInterrupt& param, bool force)
{
    auto& id = rprim->GetId();
    if (HdChangeTracker::IsPrimIdDirty(dirtyBits, id)) {
        param.Interrupt();
        _SetPrimId(rprim->GetPrimId());
    }
    if (dirtyBits & HdChangeTracker::DirtyCategories) {
        param.Interrupt();
        renderDelegate->ApplyLightLinking(_shape, sceneDelegate->GetCategories(id));
    }
    // If render tags are empty, we are displaying everything.
    if (dirtyBits & HdChangeTracker::DirtyRenderTag) {
        const auto& renderTags = renderDelegate->GetRenderTags();
        const auto renderTag = sceneDelegate->GetRenderTag(id);
        AiNodeSetDisabled(_shape, std::find(renderTags.begin(), renderTags.end(), renderTag) == renderTags.end());
        renderDelegate->RegisterRenderTag(_shape, renderTag);
    }
    _SyncInstances(dirtyBits, renderDelegate, sceneDelegate, param, id, rprim->GetInstancerId(), force);
}

void HdArnoldShape::SetVisibility(uint8_t visibility)
{
    // Either the shape is not instanced or the instances are not yet created. In either case we can set the visibility
    // on the shape.
    if (_instancer == nullptr) {
        AiNodeSetByte(_shape, str::visibility, visibility);
    }
    _visibility = visibility;
}

void HdArnoldShape::_SetPrimId(int32_t primId)
{
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
        _UpdateInstanceVisibility(param);
        return;
    }
    param.Interrupt();
    // We need to hide the source mesh.
    AiNodeSetByte(_shape, str::visibility, 0);
    auto& renderIndex = sceneDelegate->GetRenderIndex();
    auto* instancer = static_cast<HdArnoldInstancer*>(renderIndex.GetInstancer(instancerId));
    HdArnoldSampledMatrixArrayType instanceMatrices;
    instancer->CalculateInstanceMatrices(id, instanceMatrices);
    if (_instancer == nullptr) {
        _instancer = AiNode(renderDelegate->GetUniverse(), str::instancer);
        std::stringstream ss;
        ss << AiNodeGetName(_shape) << "_instancer";
        AiNodeSetStr(_instancer, str::name, AtString(ss.str().c_str()));
        AiNodeSetPtr(_instancer, str::nodes, _shape);
        AiNodeDeclare(_instancer, str::instance_inherit_xform, "constant array BOOL");
        AiNodeSetArray(_instancer, str::instance_inherit_xform, AiArray(1, 1, AI_TYPE_BOOLEAN, true));
    }
    if (instanceMatrices.count == 0 || instanceMatrices.values.front().empty()) {
        AiNodeResetParameter(_instancer, str::instance_matrix);
        AiNodeResetParameter(_instancer, str::node_idxs);
        AiNodeResetParameter(_instancer, str::instance_visibility);
    } else {
        const auto sampleCount = instanceMatrices.count;
        const auto instanceCount = instanceMatrices.values.front().size();
        auto* matrixArray = AiArrayAllocate(instanceCount, sampleCount, AI_TYPE_MATRIX);
        auto* nodeIdxsArray = AiArrayAllocate(instanceCount, sampleCount, AI_TYPE_UINT);
        auto* matrices = static_cast<AtMatrix*>(AiArrayMap(matrixArray));
        auto* nodeIdxs = static_cast<uint32_t*>(AiArrayMap(nodeIdxsArray));
        std::fill(nodeIdxs, nodeIdxs + instanceCount, 0);
        AiArrayUnmap(nodeIdxsArray);
        auto convertMatrices = [&](size_t sample) {
            std::transform(
                instanceMatrices.values[sample].begin(), instanceMatrices.values[sample].end(),
                matrices + sample * instanceCount,
                [](const GfMatrix4d& in) -> AtMatrix { return HdArnoldConvertMatrix(in); });
        };
        convertMatrices(0);
        for (auto sample = decltype(sampleCount){1}; sample < sampleCount; sample += 1) {
            // We check if there is enough data to do the conversion, otherwise we are reusing the first sample.
            if (ARCH_UNLIKELY(instanceMatrices.values[sample].size() != instanceCount)) {
                std::copy(matrices, matrices + instanceCount, matrices + sample * instanceCount);
            } else {
                convertMatrices(sample);
            }
        }
        auto setMotionParam = [&](const char* name, float value) {
            if (AiNodeLookUpUserParameter(_instancer, AtString(name)) == nullptr) {
                AiNodeDeclare(_instancer, AtString(name), str::constantArrayFloat);
            }
            AiNodeSetArray(_instancer, AtString(name), AiArray(1, 1, AI_TYPE_FLOAT, value));
        };
        if (sampleCount > 1) {
            setMotionParam(str::instance_motion_start, instanceMatrices.times.front());
            setMotionParam(str::instance_motion_end, instanceMatrices.times[sampleCount - 1]);
        } else {
            setMotionParam(str::instance_motion_start, 0.0f);
            setMotionParam(str::instance_motion_end, 1.0f);
        }
        AiArrayUnmap(matrixArray);
        AiNodeSetArray(_instancer, str::instance_matrix, matrixArray);
        AiNodeSetArray(_instancer, str::node_idxs, nodeIdxsArray);
        AiNodeSetArray(_instancer, str::instance_visibility, AiArray(1, 1, AI_TYPE_BYTE, _visibility));
        instancer->SetPrimvars(_instancer, id, instanceCount);
    }
}

void HdArnoldShape::_UpdateInstanceVisibility(HdArnoldRenderParamInterrupt& param)
{
    auto* instanceVisibility = AiNodeGetArray(_instancer, str::instance_visibility);
    const auto currentVisibility = (instanceVisibility != nullptr && AiArrayGetNumElements(instanceVisibility) == 1)
                                       ? AiArrayGetByte(instanceVisibility, 0)
                                       : ~_visibility;
    if (currentVisibility == _visibility) {
        return;
    }
    param.Interrupt();
    AiNodeSetArray(_instancer, str::instance_visibility, AiArray(1, 1, AI_TYPE_BYTE, _visibility));
}

PXR_NAMESPACE_CLOSE_SCOPE
