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
#include "scene_delegate.h"

#include "adapter_registry.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

ImagingArnoldSceneDelegate::ImagingArnoldSceneDelegate(HdRenderIndex* parentIndex, const SdfPath& delegateID)
    : HdSceneDelegate(parentIndex, delegateID)
{
}

ImagingArnoldSceneDelegate::~ImagingArnoldSceneDelegate() {}

void ImagingArnoldSceneDelegate::Sync(HdSyncRequestVector* request) {}

void ImagingArnoldSceneDelegate::PostSyncCleanup() {}

bool ImagingArnoldSceneDelegate::IsEnabled(const TfToken& option) const
{
    // We support parallel syncing of RPrim data.
    if (option == HdOptionTokens->parallelRprimSync) {
        return true;
    }
    // Unknown parameter.
    return false;
}

HdMeshTopology ImagingArnoldSceneDelegate::GetMeshTopology(const SdfPath& id) { return {}; }

HdBasisCurvesTopology ImagingArnoldSceneDelegate::GetBasisCurvesTopology(const SdfPath& id) { return {}; }

PxOsdSubdivTags ImagingArnoldSceneDelegate::GetSubdivTags(const SdfPath& id) { return {}; }

GfRange3d ImagingArnoldSceneDelegate::GetExtent(const SdfPath& id) { return {}; }

GfMatrix4d ImagingArnoldSceneDelegate::GetTransform(const SdfPath& id) { return {}; }

bool ImagingArnoldSceneDelegate::GetVisible(const SdfPath& id) { return true; }

bool ImagingArnoldSceneDelegate::GetDoubleSided(const SdfPath& id) { return false; }

HdCullStyle ImagingArnoldSceneDelegate::GetCullStyle(const SdfPath& id) { return HdCullStyle::HdCullStyleNothing; }

VtValue ImagingArnoldSceneDelegate::GetShadingStyle(const SdfPath& id) { return {}; }

HdDisplayStyle ImagingArnoldSceneDelegate::GetDisplayStyle(const SdfPath& id) { return {}; }

VtValue ImagingArnoldSceneDelegate::Get(const SdfPath& id, const TfToken& key) { return {}; }

HdReprSelector ImagingArnoldSceneDelegate::GetReprSelector(const SdfPath& id) { return HdReprSelector{}; }

TfToken ImagingArnoldSceneDelegate::GetRenderTag(const SdfPath& id) { return {}; }

VtArray<TfToken> ImagingArnoldSceneDelegate::GetCategories(const SdfPath& id) { return {}; }

std::vector<VtArray<TfToken>> ImagingArnoldSceneDelegate::GetInstanceCategories(const SdfPath& instancerId)
{
    return {};
}

HdIdVectorSharedPtr ImagingArnoldSceneDelegate::GetCoordSysBindings(const SdfPath& id) { return nullptr; }

size_t ImagingArnoldSceneDelegate::SampleTransform(
    const SdfPath& id, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues)
{
    return 0;
}

size_t ImagingArnoldSceneDelegate::SampleInstancerTransform(
    const SdfPath& instancerId, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues)
{
    return 0;
}

size_t ImagingArnoldSceneDelegate::SamplePrimvar(
    const SdfPath& id, const TfToken& key, size_t maxSampleCount, float* sampleTimes, VtValue* sampleValues)
{
    return 0;
}

VtIntArray ImagingArnoldSceneDelegate::GetInstanceIndices(const SdfPath& instancerId, const SdfPath& prototypeId)
{
    return {};
}

GfMatrix4d ImagingArnoldSceneDelegate::GetInstancerTransform(const SdfPath& instancerId) { return {}; }

SdfPath ImagingArnoldSceneDelegate::GetScenePrimPath(
    const SdfPath& rprimId, int instanceIndex, HdInstancerContext* instancerContext)
{
    return {};
}

SdfPath ImagingArnoldSceneDelegate::GetMaterialId(const SdfPath& rprimId) { return {}; }

VtValue ImagingArnoldSceneDelegate::GetMaterialResource(const SdfPath& materialId) { return {}; }

HdRenderBufferDescriptor ImagingArnoldSceneDelegate::GetRenderBufferDescriptor(const SdfPath& id) { return {}; }

VtValue ImagingArnoldSceneDelegate::GetLightParamValue(const SdfPath& id, const TfToken& paramName) { return {}; }

VtValue ImagingArnoldSceneDelegate::GetCameraParamValue(const SdfPath& cameraId, const TfToken& paramName)
{
    return {};
}

HdVolumeFieldDescriptorVector ImagingArnoldSceneDelegate::GetVolumeFieldDescriptors(const SdfPath& volumeId)
{
    return {};
}

TfTokenVector ImagingArnoldSceneDelegate::GetExtComputationSceneInputNames(const SdfPath& computationId) { return {}; }

HdExtComputationInputDescriptorVector ImagingArnoldSceneDelegate::GetExtComputationInputDescriptors(
    const SdfPath& computationId)
{
    return {};
}

HdExtComputationOutputDescriptorVector ImagingArnoldSceneDelegate::GetExtComputationOutputDescriptors(
    const SdfPath& computationId)
{
    return {};
}

HdExtComputationPrimvarDescriptorVector ImagingArnoldSceneDelegate::GetExtComputationPrimvarDescriptors(
    const SdfPath& id, HdInterpolation interpolationMode)
{
    return {};
}

VtValue ImagingArnoldSceneDelegate::GetExtComputationInput(const SdfPath& computationId, const TfToken& input)
{
    return {};
}

std::string ImagingArnoldSceneDelegate::GetExtComputationKernel(const SdfPath& computationId) { return {}; }

void ImagingArnoldSceneDelegate::InvokeExtComputation(const SdfPath& computationId, HdExtComputationContext* context) {}

HdPrimvarDescriptorVector ImagingArnoldSceneDelegate::GetPrimvarDescriptors(
    const SdfPath& id, HdInterpolation interpolation)
{
}

TfTokenVector ImagingArnoldSceneDelegate::GetTaskRenderTags(const SdfPath& taskId) {}

void ImagingArnoldSceneDelegate::Populate(AtUniverse* universe)
{
    const auto& registry = ImagingArnoldAdapterRegistry::GetInstance();
    auto* nodeIter = AiUniverseGetNodeIterator(universe, AI_NODE_SHAPE | AI_NODE_CAMERA);
    while (!AiNodeIteratorFinished(nodeIter)) {
        auto* node = AiNodeIteratorGetNext(nodeIter);
        const auto* nodeEntry = AiNodeGetNodeEntry(node);
        auto adapter = registry.FindAdapter(AiNodeEntryGetNameAtString(nodeEntry));
        if (adapter == nullptr) {
            continue;
        }
    }
}

SdfPath ImagingArnoldSceneDelegate::GetIdFromNodeName(const AtString& name) { return {}; }

PXR_NAMESPACE_CLOSE_SCOPE
