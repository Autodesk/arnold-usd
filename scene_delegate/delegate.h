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
/// @file scene_delegate/delegate.h
///
/// Class and utilities for creating a Hydra Scene Delegate.
#pragma once
#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/sceneDelegate.h>

#include "delegate_proxy.h"
#include "prim_adapter.h"

#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

/// @class ImagingArnoldDelegate
///
/// Class providing tools to convert an existing Arnold universe to the scene graph.
class ImagingArnoldDelegate : public HdSceneDelegate {
public:
    /// Constructor for creating the scene delegate.
    ///
    /// @param parentIndex Pointer to the Hydra render index.
    /// @param delegateID Path of the scene delegate.
    IMAGINGARNOLD_API
    ImagingArnoldDelegate(HdRenderIndex* parentIndex, const SdfPath& delegateID);
    /// Destructor of ImagingArnoldDelegate.
    IMAGINGARNOLD_API
    ~ImagingArnoldDelegate() override;

    /// Syncs a Hydra sync request vector.
    ///
    /// Currently a NOP.
    ///
    /// @param request Pointer to the Hydra sync request vector.
    IMAGINGARNOLD_API
    void Sync(HdSyncRequestVector* request) override;

    /// Cleans up after a sync.
    ///
    /// Currently a NOP.
    IMAGINGARNOLD_API
    void PostSyncCleanup() override;

    /// Tells if a given feature is enabled.
    ///
    /// @param option Name of the feature.
    /// @return True if the feature is enabled, false otherwise.
    IMAGINGARNOLD_API
    bool IsEnabled(const TfToken& option) const override;

    /// Gets the mesh topology.
    ///
    /// @param id Path to the Hydra mesh primitive.
    /// @return Hydra mesh topology of the primitive.
    IMAGINGARNOLD_API
    HdMeshTopology GetMeshTopology(const SdfPath& id) override;

    /// Gets the basis curves topology.
    ///
    /// Currently always returns an empty topology.
    ///
    /// @param id Path to the Hydra basis curves primitive.
    /// @return Hydra basis curves topology of the primitive.
    IMAGINGARNOLD_API
    HdBasisCurvesTopology GetBasisCurvesTopology(const SdfPath& id) override;

    /// Gets the subdiv tags.
    ///
    /// Currently always returns an empty subdiv tags.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return Subdiv tags of the primitive.
    IMAGINGARNOLD_API
    PxOsdSubdivTags GetSubdivTags(const SdfPath& id) override;

    /// Gets the extent.
    ///
    /// Currently returns an extent between -AI_BIG and AI_BIG.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return Extent of the primitive.
    IMAGINGARNOLD_API
    GfRange3d GetExtent(const SdfPath& id) override;

    /// Gets the transform.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return Transform of the primitive.
    IMAGINGARNOLD_API
    GfMatrix4d GetTransform(const SdfPath& id) override;

    /// Gets the visibility.
    ///
    /// Currently always returns true.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return True if the primitive is visible, false otherwise.
    IMAGINGARNOLD_API
    bool GetVisible(const SdfPath& id) override;

    /// Gets the double sidedness.
    ///
    /// Currently always returns false.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return True if the primitive is double sided, false otherwise.
    IMAGINGARNOLD_API
    bool GetDoubleSided(const SdfPath& id) override;

    /// Gets the culling style.
    ///
    /// Currently always returns HdCullStyleDontCare.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return Culling style of the primitive.
    IMAGINGARNOLD_API
    HdCullStyle GetCullStyle(const SdfPath& id) override;

    /// Gets the shading style.
    ///
    /// Currently always returns an empty value.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return VtValue holding the shading style of the primitive.
    IMAGINGARNOLD_API
    VtValue GetShadingStyle(const SdfPath& id) override;

    /// Gets the display style.
    ///
    /// Currently always returns the default display style.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return Display style of the primitive.
    IMAGINGARNOLD_API
    HdDisplayStyle GetDisplayStyle(const SdfPath& id) override;

    /// Gets a named value.
    ///
    /// @param id Path to the Hydra primitive.
    /// @param key Name of the value.
    /// @return Named value if it exists on the primitive, an empty VtValue otherwise.
    IMAGINGARNOLD_API
    VtValue Get(const SdfPath& id, const TfToken& key) override;

    /// Gets the authored repr.
    ///
    /// Currently always returns the default HdReprSelector.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return Repr selector of the primitive.
    IMAGINGARNOLD_API
    HdReprSelector GetReprSelector(const SdfPath& id) override;

    /// Gets the render tag that will be used to bucket primitives.
    ///
    /// Currently always returns geometry.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return Render tag of the primitive.
    IMAGINGARNOLD_API
    TfToken GetRenderTag(const SdfPath& id) override;

    /// Get the categories.
    ///
    /// Currently always returns an empty vector.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return Categories of the primitive.
    IMAGINGARNOLD_API
    VtArray<TfToken> GetCategories(const SdfPath& id) override;

    /// Returns the categories for all the instances.
    ///
    /// @param instancerId Path to the Hydra instancer.
    /// @return Categories of the instanced primitives.
    IMAGINGARNOLD_API
    std::vector<VtArray<TfToken>> GetInstanceCategories(const SdfPath& instancerId) override;

    /// Gets the coordinate system binding.
    ///
    /// Currently always returns a nullptr.
    ///
    /// @param id Path to the Hydra primitive.
    /// @return
    IMAGINGARNOLD_API
    HdIdVectorSharedPtr GetCoordSysBindings(const SdfPath& id) override;

    /// Samples the transformation.
    ///
    /// Currently always returns a single sample using ImagingArnoldDelegate::GetTransform.
    ///
    /// @param id Path to the Hydra primitive.
    /// @param maxSampleCount Maximum number of samples to query, equal to the number of values held by the output
    /// pointers.
    /// @param sampleTimes Output pointer to the float time of each sample.
    /// @param sampleValues Output pointer to the GfMatrix4d of each sample.
    /// @return Number of samples written to the output arrays.
    IMAGINGARNOLD_API
    size_t SampleTransform(
        const SdfPath& id, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues) override;

    /// Samples the instancer transformation.
    ///
    /// Currently always returns 0 and does not modify the output pointers.
    ///
    /// @param id Path to the Hydra instancer.
    /// @param maxSampleCount Maximum number of samples to query, equal to the number of values held by the output
    /// pointers.
    /// @param sampleTimes Output pointer to the float time of each sample.
    /// @param sampleValues Output pointer to the GfMatrix4d of each sample.
    /// @return Number of samples written to the output arrays.
    IMAGINGARNOLD_API
    size_t SampleInstancerTransform(
        const SdfPath& instancerId, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues) override;

    /// Samples a primvar.
    ///
    /// Currently returns 0 and does not modify the output arrays.
    ///
    /// @param id Path to the Hydra primitive.
    /// @param key Name of the primvar.
    /// @param maxSampleCount Maximum number of samples to query.
    /// @param sampleTimes Output pointer to the float time of each sample.
    /// @param sampleValues Output pointer to the VtValue of each sample.
    /// @return Number of samples written to the output arrays.
    IMAGINGARNOLD_API
    size_t SamplePrimvar(
        const SdfPath& id, const TfToken& key, size_t maxSampleCount, float* sampleTimes,
        VtValue* sampleValues) override;

    /// Gets the instance indices of a prototype used in the instancer.
    ///
    /// Currently always return an empty array.
    ///
    /// @param instancerId Path to the Hydra instancer.
    /// @param prototypeId Path of a prototype instanced.
    /// @return Instance indices of the prototype.
    IMAGINGARNOLD_API
    VtIntArray GetInstanceIndices(const SdfPath& instancerId, const SdfPath& prototypeId) override;

    /// Gets the instancer transform.
    ///
    /// Currently always returns the default GfMatrix4d.
    ///
    /// @param instancerId Path to the Hydra instancer.
    /// @return Transform of the instancer.
    IMAGINGARNOLD_API
    GfMatrix4d GetInstancerTransform(const SdfPath& instancerId) override;

    /// Gets the scene address of the prim corresponding to the given rprim and instancer index.
    ///
    /// Currently always returns an empty path.
    ///
    /// @param rprimId ID of the Hydra primitive.
    /// @param instanceIndex Index of the instance.
    /// @param instancerContext Pointer to the Hydra instancer context.
    /// @return Path of the rprim/instance in scene namespace.
    IMAGINGARNOLD_API
    SdfPath GetScenePrimPath(
        const SdfPath& rprimId, int instanceIndex, HdInstancerContext* instancerContext = nullptr) override;

    /// Gets the material ID.
    ///
    /// Currently always returns an empty path.
    ///
    /// @param rprimId Path to the Hydra primitive.
    /// @return ID of the material assigned to the primitive.
    IMAGINGARNOLD_API
    SdfPath GetMaterialId(const SdfPath& rprimId) override;

    /// Gets a material resource.
    ///
    /// Currently always returns an empty VtValue.
    ///
    /// @param materialId ID of the material.
    /// @return VtValue holding the material resource.
    IMAGINGARNOLD_API
    VtValue GetMaterialResource(const SdfPath& materialId) override;

    /// Gets a render buffer descriptor.
    ///
    /// Currently always returns the default render buffer descriptor.
    ///
    /// @param id ID of the render buffer.
    /// @return Descriptor of the render buffer.
    IMAGINGARNOLD_API
    HdRenderBufferDescriptor GetRenderBufferDescriptor(const SdfPath& id) override;

    /// Gets a named parameter of a light.
    ///
    /// Currently always returns an empty value.
    ///
    /// @param id Path to the Hydra light.
    /// @param paramName Name of the parameter.
    /// @return VtValue holding the parameter, empty VtValue if the parameter is not available.
    IMAGINGARNOLD_API
    VtValue GetLightParamValue(const SdfPath& id, const TfToken& paramName) override;

    /// Gets a named parameter of a camera.
    ///
    /// @param id Path to the Hydra camera.
    /// @param paramName Name of the parameter.
    /// @return VtValue holding the parameter, empty VtValue if the parameter is not available.
    IMAGINGARNOLD_API
    VtValue GetCameraParamValue(const SdfPath& cameraId, const TfToken& paramName) override;

    /// Gets the descriptor of a volume field.
    ///
    /// Currently always returns the default descriptor.
    ///
    /// @param volumeId Path to the Hydra volume field.
    /// @return Descriptor of the volume field.
    IMAGINGARNOLD_API
    HdVolumeFieldDescriptorVector GetVolumeFieldDescriptors(const SdfPath& volumeId) override;

    /// Gets the inputs for a given ext computation.
    ///
    /// Currently always returns an empty vector.
    ///
    /// @param computationId ID of the ext computation.
    /// @return Inputs required to perform the ext computation.
    IMAGINGARNOLD_API
    TfTokenVector GetExtComputationSceneInputNames(const SdfPath& computationId) override;

    /// Gets the computation input descriptors.
    ///
    /// Currently  alwaysreturns an empty vector.
    ///
    /// @param computationId ID of the ext computation.
    /// @return Input descriptors of the ext computation.
    IMAGINGARNOLD_API
    HdExtComputationInputDescriptorVector GetExtComputationInputDescriptors(const SdfPath& computationId) override;

    /// Gets the computation output descriptors.
    ///
    /// Currently always returns an empty vector.
    ///
    /// @param computationId ID of the ext computation.
    /// @return Output descriptors of the ext computation.
    IMAGINGARNOLD_API
    HdExtComputationOutputDescriptorVector GetExtComputationOutputDescriptors(const SdfPath& computationId) override;

    /// Gets a list of primvar names that should be bound to a generated output from anext computation for the given
    /// prim id and interpolation mode.
    ///
    /// Currently always returns an empty vector.
    ///
    /// @param id Path to the Hydra primitive.
    /// @param interpolationMode Interpolation mode of the primvars.
    /// @return List of primvars.
    IMAGINGARNOLD_API
    HdExtComputationPrimvarDescriptorVector GetExtComputationPrimvarDescriptors(
        const SdfPath& id, HdInterpolation interpolationMode) override;

    /// Gets a computation input or computation config parameter.
    ///
    /// Currently always returns an empty value.
    ///
    /// @param computationId ID of the computation.
    /// @param input Name of the input or config.
    /// @return Value holding the named input or config, empty VtValue if not available.
    IMAGINGARNOLD_API
    VtValue GetExtComputationInput(const SdfPath& computationId, const TfToken& input) override;

    /// Gets the kernel of an ext computation.
    ///
    /// Currently always returns an empty string.
    ///
    /// @param computationId ID of the ext computation.
    /// @return Kernel of the computation, empty string if not available.
    IMAGINGARNOLD_API
    std::string GetExtComputationKernel(const SdfPath& computationId) override;

    /// Invokes an ext computation.
    ///
    /// Currently a NOP.
    ///
    /// @param computationId ID of the computation.
    /// @param context Context used for the ext computation.
    IMAGINGARNOLD_API
    void InvokeExtComputation(const SdfPath& computationId, HdExtComputationContext* context) override;

    /// Get the primvar descriptors for a primitive.
    ///
    /// @param id Path to the Hydra primitive.
    /// @param interpolation Interpolation of the primvars to query.
    /// @return Primvar descriptors of a given interpolation type, empty vector if no primvars are available.
    IMAGINGARNOLD_API
    HdPrimvarDescriptorVector GetPrimvarDescriptors(const SdfPath& id, HdInterpolation interpolation) override;

    /// Gets the task aspects.
    ///
    /// Currently always returns an empty vector.
    ///
    /// @param taskId ID of the task.
    /// @return Task aspect of the task, empty vector if not available.
    IMAGINGARNOLD_API
    TfTokenVector GetTaskRenderTags(const SdfPath& taskId) override;

    /// Populating the Render Index from the Arnold universe.
    ///
    /// @param universe Input universe to use for populating the render index.
    IMAGINGARNOLD_API
    virtual void Populate(AtUniverse* universe);

    /// Gets an path to the prim in the Hydra render index from an Arnold Node name.
    ///
    /// @param name Name of the arnold node.
    /// @return Path to the primitive in the Hydra render index.
    IMAGINGARNOLD_API
    SdfPath GetIdFromNodeName(const std::string& name);

    /// Gets an path to the prim in the Hydra render index from an Arnold Node.
    ///
    /// @param node Pointer to the Arnold node.
    /// @return Path to the primitive in the Hydra render index.
    IMAGINGARNOLD_API
    SdfPath GetIdFromNode(const AtNode* node);

private:
    /// Utility struct to hold a primitive entry.
    struct PrimEntry {
        PrimEntry(ImagingArnoldPrimAdapterPtr _adapter, AtNode* _node) : adapter(_adapter), node(_node) {}
        /// Pointer to the adapter.
        ImagingArnoldPrimAdapterPtr adapter;
        /// Pointer to the Arnold node.
        AtNode* node = nullptr;
    };

    /// List of primitive entries.
    std::unordered_map<SdfPath, PrimEntry, SdfPath::Hash> _primEntries;
    /// Proxy delegate for the adapters.
    ImagingArnoldDelegateProxy _proxy;
};

PXR_NAMESPACE_CLOSE_SCOPE
