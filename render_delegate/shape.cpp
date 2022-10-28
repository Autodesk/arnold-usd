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

#include <constant_strings.h>
#include "instancer.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

HdArnoldShape::HdArnoldShape(
    const AtString& shapeType, HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const int32_t primId)
    : _renderDelegate(renderDelegate)
{
    _shape = AiNode(renderDelegate->GetUniverse(), shapeType);
    AiNodeSetStr(_shape, str::name, AtString(id.GetText()));
    _SetPrimId(primId);
}

HdArnoldShape::~HdArnoldShape()
{
    _renderDelegate->UntrackRenderTag(_shape);
    AiNodeDestroy(_shape);
    for (auto &instancer : _instancers) {
        _renderDelegate->UntrackRenderTag(instancer);
        AiNodeDestroy(instancer);
    }
}

void HdArnoldShape::Sync(
    HdRprim* rprim, HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParamInterrupt& param,
    bool force)
{
    auto& id = rprim->GetId();
    if (HdChangeTracker::IsPrimIdDirty(dirtyBits, id)) {
        param.Interrupt();
        _SetPrimId(rprim->GetPrimId());
    }
    if (dirtyBits & HdChangeTracker::DirtyCategories) {
        param.Interrupt();
        const SdfPath &instancerId = rprim->GetInstancerId();
        VtArray<TfToken> instancerCategories;
        // If this shape is instanced, we store the list of "categories"
        // (aka collections) associated with it.
        if (!instancerId.IsEmpty()) {
            instancerCategories = sceneDelegate->GetCategories(instancerId);
        }
        if (instancerCategories.empty()) {
            // If there are no collections associated with eventual instancers,
            // we just pass the reference to the categories array to avoid useless copies
            _renderDelegate->ApplyLightLinking(_shape, sceneDelegate->GetCategories(id));
        } else {
            // We want to concatenate the shape's categories with the
            // instancer's categories, and call ApplyLightLinking with the full list
            VtArray<TfToken> categories = sceneDelegate->GetCategories(id);
            categories.reserve(categories.size() + instancerCategories.size());
            for (const auto &instanceCategory : instancerCategories)
                categories.push_back(instanceCategory);
            _renderDelegate->ApplyLightLinking(_shape, categories);
        }
    }
    // If render tags are empty, we are displaying everything.
    if (dirtyBits & HdChangeTracker::DirtyRenderTag) {
        param.Interrupt();
        const auto renderTag = sceneDelegate->GetRenderTag(id);
        _renderDelegate->TrackRenderTag(_shape, renderTag);
        for (auto &instancer : _instancers) {
            _renderDelegate->TrackRenderTag(instancer, renderTag);
        }
    }
    _SyncInstances(dirtyBits, _renderDelegate, sceneDelegate, param, id, rprim->GetInstancerId(), force);
}

void HdArnoldShape::SetVisibility(uint8_t visibility)
{
    // Either the shape is not instanced or the instances are not yet created. In either case we can set the visibility
    // on the shape.
    if (_instancers.empty()) {
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
    for (auto &instancerNode : _instancers) {
        AiNodeDestroy(instancerNode);
    }
    _instancers.clear();
    instancer->CalculateInstanceMatrices(renderDelegate, id, _instancers);
    const TfToken renderTag = sceneDelegate->GetRenderTag(id);

    for (size_t i = 0; i < _instancers.size(); ++i) {
        AiNodeSetPtr(_instancers[i], str::nodes, (i == 0) ? _shape : _instancers[i - 1]);
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
}

void HdArnoldShape::_UpdateInstanceVisibility(HdArnoldRenderParamInterrupt& param)
{
    if (_instancers.empty())
        return;

    param.Interrupt();
    for (auto &instancer : _instancers) {
        AtArray* instanceVisibility = AiNodeGetArray(instancer, str::instance_visibility);
        unsigned int instVisibilityCount = (instanceVisibility) ? AiArrayGetNumElements(instanceVisibility) : 0;
        
        if (instVisibilityCount == 0) {
            AiNodeSetArray(instancer, str::instance_visibility, AiArray(1, 1, AI_TYPE_BYTE, _visibility));
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
                AiNodeSetArray(instancer, str::instance_visibility, instanceVisibility);
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
