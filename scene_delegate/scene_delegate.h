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
/// @file scene_delegate/scene_delegate.h
///
/// Class and utilities for creating a Hydra Scene Delegate.
#pragma once
#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/sceneDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

class ImagingArnoldSceneDelegate : public HdSceneDelegate {
public:
    IMAGINGARNOLD_API
    ImagingArnoldSceneDelegate(HdRenderIndex* parentIndex, const SdfPath& delegateID);
    IMAGINGARNOLD_API
    ~ImagingArnoldSceneDelegate() override;

    IMAGINGARNOLD_API
    void Sync(HdSyncRequestVector* request) override;

    IMAGINGARNOLD_API
    void PostSyncCleanup() override;

    IMAGINGARNOLD_API
    bool IsEnabled(const TfToken& option) const override;

    IMAGINGARNOLD_API
    HdMeshTopology GetMeshTopology(const SdfPath& id) override;

    IMAGINGARNOLD_API
    HdBasisCurvesTopology GetBasisCurvesTopology(const SdfPath& id) override;

    IMAGINGARNOLD_API
    PxOsdSubdivTags GetSubdivTags(const SdfPath& id) override;

    IMAGINGARNOLD_API
    GfRange3d GetExtent(const SdfPath& id) override;

    IMAGINGARNOLD_API
    GfMatrix4d GetTransform(const SdfPath& id) override;

    IMAGINGARNOLD_API
    bool GetVisible(const SdfPath& id) override;

    IMAGINGARNOLD_API
    bool GetDoubleSided(const SdfPath& id) override;

    IMAGINGARNOLD_API
    HdCullStyle GetCullStyle(const SdfPath& id) override;

    IMAGINGARNOLD_API
    VtValue GetShadingStyle(const SdfPath& id) override;

    IMAGINGARNOLD_API
    HdDisplayStyle GetDisplayStyle(const SdfPath& id) override;

    IMAGINGARNOLD_API
    VtValue Get(const SdfPath& id, const TfToken& key) override;

    IMAGINGARNOLD_API
    HdReprSelector GetReprSelector(const SdfPath& id) override;

    IMAGINGARNOLD_API
    TfToken GetRenderTag(const SdfPath& id) override;

    IMAGINGARNOLD_API
    VtArray<TfToken> GetCategories(const SdfPath& id) override;

    IMAGINGARNOLD_API
    std::vector<VtArray<TfToken>> GetInstanceCategories(const SdfPath& instancerId) override;

    IMAGINGARNOLD_API
    HdIdVectorSharedPtr GetCoordSysBindings(const SdfPath& id) override;

    IMAGINGARNOLD_API
    size_t SampleTransform(
        const SdfPath& id, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues) override;

    IMAGINGARNOLD_API
    size_t SampleInstancerTransform(
        const SdfPath& instancerId, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues) override;

    IMAGINGARNOLD_API
    size_t SamplePrimvar(
        const SdfPath& id, const TfToken& key, size_t maxSampleCount, float* sampleTimes,
        VtValue* sampleValues) override;

    IMAGINGARNOLD_API
    VtIntArray GetInstanceIndices(const SdfPath& instancerId, const SdfPath& prototypeId) override;

    IMAGINGARNOLD_API
    GfMatrix4d GetInstancerTransform(const SdfPath& instancerId) override;

    IMAGINGARNOLD_API
    SdfPath GetScenePrimPath(
        const SdfPath& rprimId, int instanceIndex, HdInstancerContext* instancerContext = nullptr) override;

    IMAGINGARNOLD_API
    SdfPath GetMaterialId(const SdfPath& rprimId) override;

    IMAGINGARNOLD_API
    VtValue GetMaterialResource(const SdfPath& materialId) override;

    IMAGINGARNOLD_API
    HdTextureResource::ID GetTextureResourceID(const SdfPath& textureId) override;

    IMAGINGARNOLD_API
    HdTextureResourceSharedPtr GetTextureResource(const SdfPath& textureId) override;

    IMAGINGARNOLD_API
    HdRenderBufferDescriptor GetRenderBufferDescriptor(const SdfPath& id) override;

    IMAGINGARNOLD_API
    VtValue GetLightParamValue(const SdfPath& id, const TfToken& paramName) override;

    IMAGINGARNOLD_API
    VtValue GetCameraParamValue(const SdfPath& cameraId, const TfToken& paramName) override;

    IMAGINGARNOLD_API
    HdVolumeFieldDescriptorVector GetVolumeFieldDescriptors(const SdfPath& volumeId) override;

    IMAGINGARNOLD_API
    TfTokenVector GetExtComputationSceneInputNames(const SdfPath& computationId) override;

    IMAGINGARNOLD_API
    HdExtComputationInputDescriptorVector GetExtComputationInputDescriptors(const SdfPath& computationId) override;

    IMAGINGARNOLD_API
    HdExtComputationOutputDescriptorVector GetExtComputationOutputDescriptors(const SdfPath& computationId) override;

    IMAGINGARNOLD_API
    HdExtComputationPrimvarDescriptorVector GetExtComputationPrimvarDescriptors(
        const SdfPath& id, HdInterpolation interpolationMode) override;

    IMAGINGARNOLD_API
    VtValue GetExtComputationInput(const SdfPath& computationId, const TfToken& input) override;

    IMAGINGARNOLD_API
    std::string GetExtComputationKernel(const SdfPath& computationId) override;

    IMAGINGARNOLD_API
    void InvokeExtComputation(const SdfPath& computationId, HdExtComputationContext* context) override;

    IMAGINGARNOLD_API
    HdPrimvarDescriptorVector GetPrimvarDescriptors(const SdfPath& id, HdInterpolation interpolation) override;

    IMAGINGARNOLD_API
    TfTokenVector GetTaskRenderTags(const SdfPath& taskId) override;

    /// Populating the Render Index from the Arnold universe.
    ///
    /// @param universe Input universe to use for populating the render index.
    IMAGINGARNOLD_API
    virtual void Populate(AtUniverse* universe);
};

PXR_NAMESPACE_CLOSE_SCOPE
