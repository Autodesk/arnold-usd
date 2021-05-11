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
#include "delegate.h"

#include "adapter_registry.h"

#include <constant_strings.h>

PXR_NAMESPACE_OPEN_SCOPE

ImagingArnoldDelegate::ImagingArnoldDelegate(HdRenderIndex* parentIndex, const SdfPath& delegateID)
    : HdSceneDelegate(parentIndex, delegateID), _proxy(this)
{
}

ImagingArnoldDelegate::~ImagingArnoldDelegate() {}

void ImagingArnoldDelegate::Sync(HdSyncRequestVector* request) {}

void ImagingArnoldDelegate::PostSyncCleanup() {}

bool ImagingArnoldDelegate::IsEnabled(const TfToken& option) const
{
    // We support parallel syncing of RPrim data.
    if (option == HdOptionTokens->parallelRprimSync) {
        return true;
    }
    // Unknown parameter.
    return false;
}

HdMeshTopology ImagingArnoldDelegate::GetMeshTopology(const SdfPath& id)
{
    auto* entry = TfMapLookupPtr(_primEntries, id);
    return Ai_unlikely(entry == nullptr) ? HdMeshTopology() : entry->adapter->GetMeshTopology(entry->node);
}

HdBasisCurvesTopology ImagingArnoldDelegate::GetBasisCurvesTopology(const SdfPath& id) { return {}; }

PxOsdSubdivTags ImagingArnoldDelegate::GetSubdivTags(const SdfPath& id) { return {}; }

GfRange3d ImagingArnoldDelegate::GetExtent(const SdfPath& id)
{
    // TODO(pal): Should we cache this? Or we expect the render delegates to cache?
    auto* entry = TfMapLookupPtr(_primEntries, id);
    return Ai_unlikely(entry == nullptr) ? GfRange3d{} : entry->adapter->GetExtent(entry->node);
}

GfMatrix4d ImagingArnoldDelegate::GetTransform(const SdfPath& id)
{
    auto* entry = TfMapLookupPtr(_primEntries, id);
    return Ai_unlikely(entry == nullptr) ? GfMatrix4d(1.0) : entry->adapter->GetTransform(entry->node);
}

bool ImagingArnoldDelegate::GetVisible(const SdfPath& id) { return true; }

bool ImagingArnoldDelegate::GetDoubleSided(const SdfPath& id) { return false; }

HdCullStyle ImagingArnoldDelegate::GetCullStyle(const SdfPath& id) { return HdCullStyle::HdCullStyleDontCare; }

VtValue ImagingArnoldDelegate::GetShadingStyle(const SdfPath& id) { return {}; }

HdDisplayStyle ImagingArnoldDelegate::GetDisplayStyle(const SdfPath& id) { return {}; }

VtValue ImagingArnoldDelegate::Get(const SdfPath& id, const TfToken& key)
{
    auto* entry = TfMapLookupPtr(_primEntries, id);
    return Ai_unlikely(entry == nullptr) ? VtValue{} : entry->adapter->Get(entry->node, key);
}

HdReprSelector ImagingArnoldDelegate::GetReprSelector(const SdfPath& id) { return HdReprSelector{}; }

TfToken ImagingArnoldDelegate::GetRenderTag(const SdfPath& id) { return HdRenderTagTokens->geometry; }

VtArray<TfToken> ImagingArnoldDelegate::GetCategories(const SdfPath& id) { return {}; }

std::vector<VtArray<TfToken>> ImagingArnoldDelegate::GetInstanceCategories(const SdfPath& instancerId) { return {}; }

HdIdVectorSharedPtr ImagingArnoldDelegate::GetCoordSysBindings(const SdfPath& id) { return nullptr; }

size_t ImagingArnoldDelegate::SampleTransform(
    const SdfPath& id, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues)
{
    if (maxSampleCount > 0) {
        sampleTimes[0] = 0.0f;
        sampleValues[0] = GetTransform(id);
        return 1;
    }
    return 0;
}

size_t ImagingArnoldDelegate::SampleInstancerTransform(
    const SdfPath& instancerId, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues)
{
    return 0;
}

size_t ImagingArnoldDelegate::SamplePrimvar(
    const SdfPath& id, const TfToken& key, size_t maxSampleCount, float* sampleTimes, VtValue* sampleValues)
{
    return 0;
}

VtIntArray ImagingArnoldDelegate::GetInstanceIndices(const SdfPath& instancerId, const SdfPath& prototypeId)
{
    return {};
}

GfMatrix4d ImagingArnoldDelegate::GetInstancerTransform(const SdfPath& instancerId) { return {}; }

SdfPath ImagingArnoldDelegate::GetScenePrimPath(
    const SdfPath& rprimId, int instanceIndex, HdInstancerContext* instancerContext)
{
    return {};
}

SdfPath ImagingArnoldDelegate::GetMaterialId(const SdfPath& rprimId) { return {}; }

VtValue ImagingArnoldDelegate::GetMaterialResource(const SdfPath& materialId) { return {}; }

HdRenderBufferDescriptor ImagingArnoldDelegate::GetRenderBufferDescriptor(const SdfPath& id) { return {}; }

VtValue ImagingArnoldDelegate::GetLightParamValue(const SdfPath& id, const TfToken& paramName) { return {}; }

VtValue ImagingArnoldDelegate::GetCameraParamValue(const SdfPath& cameraId, const TfToken& paramName)
{
    auto* entry = TfMapLookupPtr(_primEntries, cameraId);
    return Ai_unlikely(entry == nullptr) ? VtValue{} : entry->adapter->Get(entry->node, paramName);
}

HdVolumeFieldDescriptorVector ImagingArnoldDelegate::GetVolumeFieldDescriptors(const SdfPath& volumeId) { return {}; }

TfTokenVector ImagingArnoldDelegate::GetExtComputationSceneInputNames(const SdfPath& computationId) { return {}; }

HdExtComputationInputDescriptorVector ImagingArnoldDelegate::GetExtComputationInputDescriptors(
    const SdfPath& computationId)
{
    return {};
}

HdExtComputationOutputDescriptorVector ImagingArnoldDelegate::GetExtComputationOutputDescriptors(
    const SdfPath& computationId)
{
    return {};
}

HdExtComputationPrimvarDescriptorVector ImagingArnoldDelegate::GetExtComputationPrimvarDescriptors(
    const SdfPath& id, HdInterpolation interpolationMode)
{
    return {};
}

VtValue ImagingArnoldDelegate::GetExtComputationInput(const SdfPath& computationId, const TfToken& input) { return {}; }

std::string ImagingArnoldDelegate::GetExtComputationKernel(const SdfPath& computationId) { return {}; }

void ImagingArnoldDelegate::InvokeExtComputation(const SdfPath& computationId, HdExtComputationContext* context) {}

HdPrimvarDescriptorVector ImagingArnoldDelegate::GetPrimvarDescriptors(const SdfPath& id, HdInterpolation interpolation)
{
    auto* entry = TfMapLookupPtr(_primEntries, id);
    return Ai_unlikely(entry == nullptr) ? HdPrimvarDescriptorVector{}
                                         : entry->adapter->GetPrimvarDescriptors(entry->node, interpolation);
}

TfTokenVector ImagingArnoldDelegate::GetTaskRenderTags(const SdfPath& taskId) {}

void ImagingArnoldDelegate::Populate(AtUniverse* universe)
{
    // Does it make sense to parallelize this? It should be pretty lightweight, and most of the render index
    // insertion would need to be locked anyway.
    // For example, UsdImagingDelegate caches lots of the values when Populating the scene, we don't use an intermediate
    // cache, just do the conversions on the fly.
    const auto& registry = ImagingArnoldAdapterRegistry::GetInstance();
    auto* nodeIter = AiUniverseGetNodeIterator(universe, AI_NODE_SHAPE | AI_NODE_CAMERA);
    while (!AiNodeIteratorFinished(nodeIter)) {
        auto* node = AiNodeIteratorGetNext(nodeIter);
        // ginstances handled separately, instancer should not show up because we expect an expanded universe.
        if (AiNodeIs(node, str::ginstance)) {
            continue;
        }
        const auto* nodeEntry = AiNodeGetNodeEntry(node);
        auto adapter = registry.FindAdapter(AiNodeEntryGetNameAtString(nodeEntry));
        if (adapter == nullptr || !adapter->IsSupported(&_proxy)) {
            continue;
        }
        const auto id = GetIdFromNode(node);
        // We expect every prim adapter to only create a single prim.
        adapter->Populate(node, &_proxy, id);
        _primEntries.emplace(id, PrimEntry{adapter, node});
    }
}

SdfPath ImagingArnoldDelegate::GetIdFromNodeName(const std::string& name)
{
    auto path = name;
    // Paths have to start with /
    if (path.front() == '/') {
        path = name.substr(1);
    }

    std::locale loc;

    for (size_t i = 0; i < path.length(); ++i) {
        char& c = path[i];
        if (c == '|') {
            c = '/';
        } else if (c == '@' || c == '.' || c == ':' || c == '-') {
            c = '_';
        }
        // If the first character after each '/' is a digit, USD will complain.
        // We'll insert a dummy character in that case
        if (path[i] == '/' && i < (path.length() - 1) && std::isdigit(path[i + 1], loc)) {
            path.insert(i + 1, 1, '_');
            i++;
        }
    }
    return GetDelegateID().AppendPath(SdfPath{path});
}

SdfPath ImagingArnoldDelegate::GetIdFromNode(const AtNode* node)
{
    std::string name{AiNodeGetName(node)};
    if (name.empty()) {
        name = TfStringPrintf("unnamed/%s/%p", AiNodeEntryGetName(AiNodeGetNodeEntry(node)), node);
    }

    return GetIdFromNodeName(name);
}

PXR_NAMESPACE_CLOSE_SCOPE
