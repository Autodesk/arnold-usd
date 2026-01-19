//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2019 Luma Pictures
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
//
// Modifications Copyright 2022 Autodesk, Inc.
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
/// @file render_delegate.h
///
/// Render Delegate class for Hydra.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/base/vt/array.h>

#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>
#include <pxr/imaging/hd/resourceRegistry.h>

#include <tbb/concurrent_queue.h>

#include "hdarnold.h"
#include "render_param.h"
#include "api_adapter.h"

#include <ai.h>

class HydraArnoldReader;

PXR_NAMESPACE_OPEN_SCOPE

struct HdArnoldRenderVar {
    /// Settings for the RenderVar.
    HdAovSettingsMap settings;
    /// Name of the render var.
    std::string name;
    /// Source name of the Render Var.
    std::string sourceName;
    /// Source type of the Render Var.
    TfToken sourceType;
    /// Data Type of the Render Var.
    TfToken dataType;
    /// Format of the AOV descriptor.
    HdFormat format = HdFormatFloat32Vec4;
    /// Clear Value, currently ignored.
    VtValue clearValue;
    /// Whether or not the render var is multisampled, currently ignored.
    bool multiSampled = true;
};

struct HdArnoldDelegateRenderProduct {
    /// List of RenderVars used by the RenderProduct.
    std::vector<HdArnoldRenderVar> renderVars;
    /// Map of settings for the RenderProduct.
    HdAovSettingsMap settings;
    /// Name of the product, this is equal to the output location.
    TfToken productName;
    /// Type of the render product, set to the arnold driver entry type
    TfToken productType;
};

class HydraArnoldAPI : public ArnoldAPIAdapter {
public:
    HydraArnoldAPI(HdArnoldRenderDelegate *renderDelegate) : 
        _renderDelegate(renderDelegate) {}
    AtNode *CreateArnoldNode(const char *type, const char *name) override; 
    
    // Does the caller really need the primvars ? as hydra should have taken care of it
    const std::vector<UsdGeomPrimvar> &GetPrimvars() const override {return _primvars;}

    void AddNodeName(const std::string &name, AtNode *node) override; 
    AtNode* LookupTargetNode(const char *targetName, const AtNode* source, ConnectionType c) override; 
    const AtNode *GetProceduralParent() const;
    const AtString& GetPxrMtlxPath() override;

    HdArnoldRenderDelegate *_renderDelegate;
    // To be removed
    std::vector<UsdGeomPrimvar> _primvars;

};


/// Main class point for the Arnold Render Delegate.
class HdArnoldRenderDelegate final : public HdRenderDelegate {
public:
    HDARNOLD_API
    HdArnoldRenderDelegate(bool isBatch, const TfToken &context, 
        AtUniverse *universe = nullptr, AtSessionMode sessionTtype = AI_SESSION_INTERACTIVE, AtNode* procParent = nullptr); ///< Constructor for the Render Delegate.
    HDARNOLD_API
    ~HdArnoldRenderDelegate() override; ///< Destuctor for the Render Delegate.
    /// Returns an instance of HdArnoldRenderParam.
    ///
    /// @return Pointer to an instance of HdArnoldRenderParam.
    HDARNOLD_API
    HdRenderParam* GetRenderParam() const override;
    /// Returns the list of RPrim type names that the Render Delegate supports.
    ///
    /// This list contains renderable primitive types, like meshes, curves,
    /// volumes and so on.
    ///
    /// @return VtArray holding the name of the supported RPrim types.
    HDARNOLD_API
    const TfTokenVector& GetSupportedRprimTypes() const override;
    /// Returns the list of SPrim type names that the Render Delegate supports.
    ///
    /// This list contains state primitive types, like cameras, materials and
    /// lights.
    ///
    /// @return VtArray holding the name of the supported SPrim types.
    HDARNOLD_API
    const TfTokenVector& GetSupportedSprimTypes() const override;
    /// Returns the list of BPrim type names that the Render Delegate supports.
    ///
    /// This list contains buffer primitive types, like render buffers,
    /// openvdb assets and so on.
    ///
    /// @return VtArray holding the name of the supported BPrim types.
    HDARNOLD_API
    const TfTokenVector& GetSupportedBprimTypes() const override;
    /// Sets the Render Setting for the given key.
    ///
    /// @param key Name of the Render Setting to set.
    /// @param value Value of the Render Setting.
    HDARNOLD_API
    void SetRenderSetting(const TfToken& key, const VtValue& value) override;
    /// Gets the Render Setting for the given key.
    ///
    /// @param key Name of the Render Setting to get.
    /// @return Value of the Render Setting.
    HDARNOLD_API
    VtValue GetRenderSetting(const TfToken& _key) const override;
    /// Gets the list of Render Setting descriptors.
    ///
    /// @return std::vector holding HdRenderSettingDescriptor for all the
    ///  possible Render Settings.
    HDARNOLD_API
    HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;
    /// Returns an open-format dictionary of render statistics
    ///
    /// @return VtDictionary holding the render stats.
    HDARNOLD_API
    VtDictionary GetRenderStats() const override;
    /// Gets the Resource Registry.
    ///
    /// @return Pointer to the shared HdResourceRegistry.
    HDARNOLD_API
    HdResourceRegistrySharedPtr GetResourceRegistry() const override;
    /// Creates a new Render Pass.
    ///
    /// @param index Pointer to HdRenderIndex.
    /// @param collection RPrim collection to bind to the newly created Render
    ///  Pass.
    /// @return A shared pointer to the new Render Pass or nullptr on error.
    HDARNOLD_API
    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex* index, HdRprimCollection const& collection) override;
    /// Request to create a new instancer.
    ///
    /// @param id The unique identifier of this instancer.
    /// @return A pointer to the new instancer or nullptr on error.
    HdInstancer* CreateInstancer(HdSceneDelegate* delegate, SdfPath const& id) override;
    /// Destroys a Point Instancer.
    ///
    /// @param instancer Pointer to an instance of HdInstancer.
    HDARNOLD_API
    void DestroyInstancer(HdInstancer* instancer) override;
    /// Creates a new RPrim.
    ///
    /// @param typeId Type name of the primitive.
    /// @param rprimId Path to the primitive.
    /// @return Pointer to the newly created RPrim or nullptr on error.
    HDARNOLD_API
    HdRprim* CreateRprim(TfToken const& typeId, SdfPath const& rprimId) override;
    /// Destroys an RPrim.
    ///
    /// @param rPrim Pointer to an RPrim.
    HDARNOLD_API
    void DestroyRprim(HdRprim* rPrim) override;
    /// Creates a new SPrim.
    ///
    /// @param typeId Type of the SPrim to create.
    /// @param sprimId Path to the primitive.
    /// @return Pointer to a new SPrim or nullptr on error.
    HDARNOLD_API
    HdSprim* CreateSprim(TfToken const& typeId, SdfPath const& sprimId) override;
    /// Creates a fallback SPrim.
    ///
    /// @param typeId Type of the fallback SPrim to create.
    /// @return Pointer to a fallback SPrim or nullptr on error.
    HDARNOLD_API
    HdSprim* CreateFallbackSprim(TfToken const& typeId) override;
    /// Destroys an SPrim.
    ///
    /// @param sPrim Pointer to an SPrim.
    HDARNOLD_API
    void DestroySprim(HdSprim* sPrim) override;
    /// Creates a new BPrim.
    ///
    /// @param typeId Type of the new BPrim to create.
    /// @param bprimId Path to the primitive.
    /// @return Pointer to the newly created BPrim or nullptr on error.
    HDARNOLD_API
    HdBprim* CreateBprim(TfToken const& typeId, SdfPath const& bprimId) override;
    /// Creates a fallback BPrim.
    ///
    /// @param typeId Type of the fallback Bprim to create.
    /// @return Pointer to the fallback BPrim or nullptr on error.
    HDARNOLD_API
    HdBprim* CreateFallbackBprim(TfToken const& typeId) override;
    /// Destroys a BPrim.
    ///
    /// @param bPrim Pointer to the BPrim.
    HDARNOLD_API
    void DestroyBprim(HdBprim* bPrim) override;
    /// Commits resources to the Render Delegate.
    ///
    /// This is a callback for a Render Delegate to move, update memory for
    /// resources.
    ///
    /// @param tracker Pointer to the Change Tracker.
    HDARNOLD_API
    void CommitResources(HdChangeTracker* tracker) override;
    /// Returns a token to indicate wich material binding should be used.
    ///
    /// The function currently returns "full", to indicate production renders,
    /// not the default "preview" value.
    ///
    /// @return Name of the preferred material binding.
    HDARNOLD_API
    TfToken GetMaterialBindingPurpose() const override;
    /// Returns a list, in decending order of preference, that can be used to
    /// select among multiple material network implementations. The default
    /// list contains an empty token.
    ///
    /// Since USD 21.05 GetMaterialNetworkSelector is deprecated.
    ///
    /// @return List of material render contexts.
    HDARNOLD_API
    TfTokenVector GetMaterialRenderContexts() const override;
    /// Suffixes Node names with the Render Delegate's paths.
    ///
    /// @param name Name of the Node.
    /// @return The Node's name suffixed by the Render Delegate's path.
    HDARNOLD_API
    AtString GetLocalNodeName(const AtString& name) const;
    /// Gets the active Arnold Universe.
    ///
    /// @return Pointer to the Arnold Universe used by the Render Delegate.
    HDARNOLD_API
    AtUniverse* GetUniverse() const;
    /// Gets the active Arnold Render Session.
    ///
    /// @return Pointer to the Render Session used by the Render Delegate.
    HDARNOLD_API
    AtRenderSession* GetRenderSession() const;
    /// Gets the Arnold Options node.
    ///
    /// @return Pointer to the Arnold Options Node.
    HDARNOLD_API
    AtNode* GetOptions() const;
    /// Gets the fallback Arnold Shader.
    ///
    /// The fallback shader is a "utility" shader, with "shade_mode" of "flat",
    /// "color_mode" of "color" and a "user_data_rgba" is connected to "color",
    /// which reads the "color" attribute with the default value of
    /// AtRGBA(1.0f, 1.0f, 1.0f, 1.0).
    ///
    /// @return Pointer to the fallback Arnold Shader.
    HDARNOLD_API
    AtNode* GetFallbackSurfaceShader();
    /// Gets fallback Arnold Volume shader.
    ///
    /// The fallback shader is just an instances of standard_volume.
    ///
    /// @return Pointer to the fallback Arnold Volume Shader.
    HDARNOLD_API
    AtNode* GetFallbackVolumeShader();
    /// Gets the default settings for supported aovs.
    HDARNOLD_API
    HdAovDescriptor GetDefaultAovDescriptor(const TfToken& name) const override;

    /// Registers a light in a light linking collection.
    ///
    /// @param name Name of the collection.
    /// @param light Pointer to the Hydra Light object.
    /// @param isShadow If the clection is for shadow or light linking.
    HDARNOLD_API
    void RegisterLightLinking(const TfToken& name, HdLight* light, bool isShadow = false);

    /// Deregisters a light in a light linking collection.
    ///
    /// @param name Name of the collection.
    /// @param light Pointer to the Hydra Light object.
    /// @param isShadow If the clection is for shadow or light linking.
    HDARNOLD_API
    void DeregisterLightLinking(const TfToken& name, HdLight* light, bool isShadow = false);

    /// Apply light linking to a node and rprim id.
    /// @param delegate scene delegate.
    /// @param node Pointer to the Arnold Shape.
    /// @param id ID of the rprim.
    HDARNOLD_API
    void ApplyLightLinking(HdSceneDelegate *delegate, AtNode* node, SdfPath const& id);

    /// Eventually mark some hydra primitives as being dirty
    /// in which case we'll have another sync iteration pending
    ///
    /// @param renderIndex Pointer to the Hydra Render Index.
    /// @param shutterOpen Shutter Open value of the active camera.
    /// @param shutterClose Shutter Close value of the active camera.
    /// @return True if hydra has pending changes.
    HDARNOLD_API
    bool HasPendingChanges(HdRenderIndex* renderIndex, const SdfPath& cameraId, const GfVec2f& shutter);

    /// Returns whether the Arnold scene can be updated or
    /// if Hydra changes should be ignored.
    ///
    /// @return True if the Arnold scene can be modified
    HDARNOLD_API
    bool CanUpdateScene();

    using DelegateRenderProducts = std::vector<HdArnoldDelegateRenderProduct>;
    /// Returns the list of available Delegate Render Products.
    ///
    /// @return Const Reference to the list of Delegate Render Products.
    const DelegateRenderProducts& GetDelegateRenderProducts() const { return _delegateRenderProducts; }

    /// Check if we need to update the render delegate products.
    /// The "dirty" flag is cleaned during this function
    bool NeedsDelegateProductsUpdate() {
        const bool needsDelegateProductsUpdate = 
            !_delegateRenderProducts.empty() && _delegateRenderProductsDirty;
        _delegateRenderProductsDirty = false;
        return needsDelegateProductsUpdate;
    }

    /// Clear the existing list of delegate render products. This is needed when the render pass
    /// didn't manage to create any render product based on the delegate list
    void ClearDelegateRenderProducts() {
        _delegateRenderProducts.clear(); 
        _delegateRenderProductsDirty = true;
    }
    /// Advertise whether this delegate supports pausing and resuming of
    /// background render threads. Default implementation returns false.
    ///
    /// @return True if pause/restart is supported.
    HDARNOLD_API
    bool IsPauseSupported() const override;
    
    /// Advertise whether this delegate supports stopping and restarting of
    /// background render threads. Default implementation returns false.
    ///
    /// @return True if stop/restart is supported.
    HDARNOLD_API
    bool IsStopSupported() const override;

#if PXR_VERSION >= 2203
    /// Advertise whether the render was stopped or if it's in progress
    /// @return True if no render is in progress
    HDARNOLD_API
    bool IsStopped() const override;
#endif

#if PXR_VERSION >= 2203
    /// Stop all of this delegate's background rendering threads. Default
    /// implementation does nothing.
    ///
    /// @return True if successful.
    HDARNOLD_API
    bool Stop(bool blocking = true) override;
#else
    bool Stop() override;
#endif

    /// Restart all of this delegate's background rendering threads previously
    /// paused by a call to Stop. Default implementation does nothing.
    ///
    /// @return True if successful.
    HDARNOLD_API
    bool Restart() override;

    /// Resume all of this delegate's background rendering threads previously
    /// paused by a call to Pause. Default implementation does nothing. This is
    /// currently doing the same as restart
    HDARNOLD_API
    bool Resume() override;

    using NativeRprimParamList = std::unordered_map<TfToken, const AtParamEntry*, TfToken::HashFunctor>;

    /// Returns a list of parameters for each native rprim.
    ///
    /// @param arnoldNodeType Type of the arnold node.
    /// @return Constant Pointer to the list of parameters for each native rprim.
    HDARNOLD_API
    const NativeRprimParamList* GetNativeRprimParamList(const AtString& arnoldNodeType) const;

    using PathSet = std::unordered_set<SdfPath, SdfPath::Hash>;

    struct HashPathAndDirtyBits {
        size_t operator () (const std::pair<SdfPath, HdDirtyBits> &key) const {
            return TfHash::Combine(key.first, key.second);
        }
    };

    using PathSetWithDirtyBits = std::unordered_set<std::pair<SdfPath, HdDirtyBits>, HashPathAndDirtyBits>;

    /// Track dependencies from one prim to others
    ///
    /// @param shape Id of the prim to track.
    /// @param targets list of dependencies for a given source. This will override eventually existing ones.
    HDARNOLD_API
    void TrackDependencies(const SdfPath& source, const PathSetWithDirtyBits& targets);

    /// Untrack all dependencies assigned to a source
    ///
    /// @param source Id of the source prim
    void ClearDependencies(const SdfPath& source);

    /// Tells all nodes that rely on a dependency that they need to be updated
    ///
    /// @param id Path to the dependency target.
    HDARNOLD_API
    void DirtyDependency(const SdfPath& id);

    /// Remove node graph from the list tracking dependencies between shapes and node graphs.
    ///
    /// @param id Path to the node graph.
    HDARNOLD_API
    void RemoveDependency(const SdfPath& id);

    /// Registers a new shape and render tag.
    ///
    /// @param node Pointer to the Arnold node.
    /// @param tag Render tag of the node.
    HDARNOLD_API
    void TrackRenderTag(AtNode* node, const TfToken& tag);

    bool IsVisibleRenderTag(const TfToken& tag) const
    {
        return std::find(_renderTags.begin(), _renderTags.end(), tag) != _renderTags.end();
    }

    /// Deregisters a shape from the render tag map.
    ///
    /// @param node Pointer to the Arnold node.
    HDARNOLD_API
    void UntrackRenderTag(AtNode* node);

    /// Sets render tags to display.
    ///
    /// @param renderTags List of render tags to display.
    HDARNOLD_API
    void SetRenderTags(const TfTokenVector& renderTags);

    /// Get the background shader.
    ///
    /// @param renderIndex Pointer to the Hydra render index.
    /// @return Pointer to the background shader, nullptr if no shader is set.
    HDARNOLD_API
    AtNode* GetBackground(HdRenderIndex* renderIndex);

    /// Get the atmosphere shader.
    ///
    /// @param renderIndex Pointer to the Hydra render index.
    /// @return Pointer to the atmosphere shader, nullptr if no shader is set.
    HDARNOLD_API
    AtNode* GetAtmosphere(HdRenderIndex* renderIndex);

    /// Get the camera used for subdivision dicing
    ///
    /// @param renderIndex Pointer to the Hydra render index.
    /// @return Pointer to the camera node, nullptr if no camera is set.
    HDARNOLD_API
    AtNode* GetSubdivDicingCamera(HdRenderIndex* renderIndex);

    /// Get the aov shaders.
    ///
    /// @param renderIndex Pointer to the Hydra render index.
    /// @return Vector of Pointer to the aov shaders.
    HDARNOLD_API
    std::vector<AtNode*> GetAovShaders(HdRenderIndex* renderIndex);

    /// Get the root of the imager graph
    ///
    /// @param renderIndex Pointer to the Hydra render index.
    /// @return Pointer to the imager node.
    HDARNOLD_API
    AtNode* GetImager(HdRenderIndex* renderIndex);

    /// Get the shader_override node.
    ///
    /// @param renderIndex Pointer to the Hydra render index.
    /// @return Pointer to the shader_override shader, nullptr if no shader is set.
    HDARNOLD_API
    AtNode* GetShaderOverride(HdRenderIndex* renderIndex);

    // Store the list of cryptomatte driver names, so that we can get the cryptomatte
    // metadatas in their attribute "custom_attributes"
    /// @param driver Name of a driver used for a cryptomatte AOVs (crypto_material, crypto_asset, crypto_object)
    HDARNOLD_API
    void RegisterCryptomatteDriver(const AtString& driver);

    // Clear the list of cryptomatte driver names, before outputs are setup
    HDARNOLD_API
    void ClearCryptomatteDrivers();

    /// Get the current Window NDC, as a resolution-independant value, 
    /// defaulting to (0,0,1,1)
    ///
    /// @return Vector4 window relative to the resolution, as (minX, minY, maxX, maxY)
    HDARNOLD_API
    GfVec4f GetWindowNDC() const {return _windowNDC;}

    /// Get the current pixel aspect ratio, defaulting to 1
    ///
    /// @return Float Pixel Aspect Ratio value, as defined in the Render Settings
    
    HDARNOLD_API
    float GetPixelAspectRatio() const {return _pixelAspectRatio;}


    /// Get the current resolution, as returned from the render settings
    ///
    /// @return width / height resolution, as a Vec2i
    HDARNOLD_API
    GfVec2i GetResolution() const {return _resolution;}

    bool IsBatchContext() const {return _isBatch;}

    HydraArnoldAPI &GetAPIAdapter() {return _apiAdapter;}
    
    /// @brief Get the procedural parent
    /// @return 
    const AtNode *GetProceduralParent() const { return _procParent; }

    /// @brief Get the USD environment variable used to store custom materialx node definitions
    /// @return Environment variable, as an arnold AtString
    const AtString &GetPxrMtlxPath() const { return _pxrMtlxPath;}

    /// @brief set the node mask for translation. Default behaviour is AI_NODE_ALL which will
    /// support all arnold types. By setting it to a different value, some node types will be filtered
    /// out, following the arnold rules for node masks.
    /// @param mask as an integer, combining the different bits for node types (e.g. AI_NODE_SHAPE, AI_NODE_SHADER, etc...)
    void SetMask(int mask) {_mask = mask;}

    void SetNodeId(int id) {_nodeId = id;}

#if PXR_VERSION >= 2108
    /// Get the descriptors for the commands supported by this render delegate.
    HDARNOLD_API
    virtual HdCommandDescriptors GetCommandDescriptors() const override;

    /// Invoke a HdArnoldRenderDelegate command
    HDARNOLD_API
    virtual bool InvokeCommand(const TfToken& command, const HdCommandArgs& args = HdCommandArgs()) override;
#endif

    const std::string &GetOutputOverride() const {return _outputOverride;}
    
    /// Method used to create any node in the context of the render delegate. 
    /// This method should always be called, instead of explicit AiNode() creations
    inline 
    AtNode* CreateArnoldNode(const AtString &nodeType, const AtString &nodeName) {
        AtNode* node = AiNode(GetUniverse(), nodeType, nodeName, _procParent);
        if (_procParent) {

            // All shape nodes should have an id parameter if we're coming from a parent procedural
            if (AiNodeEntryGetType(AiNodeGetNodeEntry(node)) == AI_NODE_SHAPE) {
                AiNodeSetUInt(node, str::id, _nodeId);
            }
            
            std::lock_guard<std::mutex> lock(_nodesMutex);
            _nodes.push_back(node);
        }
        return node;
    };

    // Lookup for a node of a given name and type and return it, otherwise create a new one
    inline 
    AtNode* FindOrCreateArnoldNode(const AtString &nodeType, const AtString &nodeName) {
        AtNode* node = LookupNode(nodeName.c_str());
        if (node != nullptr && AiNodeIs(node, nodeType))
            return node;

        return CreateArnoldNode(nodeType, nodeName);
    }


    /// Method used to destroy nodes in the context of the render delegate.
    /// This method should always be called, instead of explicit AiNodeDestroy() 
    inline 
    void DestroyArnoldNode(AtNode *node)
    {
        /// When the render delegate is used through a HydraArnoldReader, it can be deleted
        /// after the translation to Arnold was completed. In that case, we want to 
        /// skip any nodes destruction, as their ownership was already passed on to Arnold.
        if (node == nullptr || !_enableNodesDestruction)
            return;
        {
            std::lock_guard<std::mutex> guard(_nodeNamesMutex);
            auto nodeIt = _nodeNames.find(AiNodeGetName(node));
            if (nodeIt != _nodeNames.end())
                _nodeNames.erase(nodeIt);
        }
        // if we have a procedural parent, we should avoid deleting nodes
        // as this can happen in batch sessions during procedural_update, 
        // which is not allowed
        if (_procParent) {
            AiNodeSetDisabled(node, true);
        }
        else
            AiNodeDestroy(node);
    }

    inline void AddNodeName(const std::string &name, AtNode *node)
    {
        std::lock_guard<std::mutex> guard(_nodeNamesMutex);
        _nodeNames[name] = node;
    }

    /// Method used to lookup a node in the current universe.
    /// This method should always be called, instead of explicit AiNodeLookUpByName
    inline 
    AtNode *LookupNode(const char *name, bool checkParent = true) const
    {
        {
            std::lock_guard<std::mutex> guard(_nodeNamesMutex);
            auto it = _nodeNames.find(std::string(name));
            if (it != _nodeNames.end())
                return it->second;        
        }
        
        AtNode *node = AiNodeLookUpByName(_universe, AtString(name), _procParent);
        // We don't want to take into account nodes that were created by a parent procedural
        // (see #172). It happens that calling AiNodeGetParent on a child node that was just
        // created by this procedural returns nullptr. I guess we'll get a correct result only
        // after the procedural initialization is finished. The best test we can do now is to
        // ignore the node returned by AiNodeLookupByName if it has a non-null parent that
        // is different from the current procedural parent
        if (checkParent && node) {
            AtNode *parent = AiNodeGetParent(node);
            if (parent != nullptr && parent != _procParent)
                node = nullptr;
        }
        return node;
    }

    void ProcessConnections();

#if ARNOLD_VERSION_NUM > 70203
    const AtNodeEntry * GetMtlxCachedNodeEntry (const std::string &nodeEntryKey, const AtString &nodeType, AtParamValueMap *params);
#endif
#if ARNOLD_VERSION_NUM >= 70104
    AtString GetCachedOslCode (const std::string &oslCacheKey, const AtString &nodeType, AtParamValueMap *params);
#endif

    std::vector<AtNode*> _nodes;

    void RegisterMeshLight(AtNode *meshLight) {
        std::lock_guard<std::mutex> guard(_meshLightsMutex);
        _meshLights.insert(meshLight);
        _meshLightsChanged.store(true, std::memory_order_release);
    }
    void UnregisterMeshLight(AtNode *meshLight) {
        std::lock_guard<std::mutex> guard(_meshLightsMutex);
        _meshLights.erase(meshLight);
        _meshLightsChanged.store(true, std::memory_order_release);
    }

    void EnableNodesDestruction(bool b) {_enableNodesDestruction = b;}
    
    // Return true if the render delegate supports shape instancing
    bool SupportShapeInstancing () const {return _supportShapeInstancing;}

    HydraArnoldReader *GetReader() {return _reader;} 
    void SetReader(HydraArnoldReader *r) {_reader = r;} 
    bool HasCryptomatte() const {return _hasCryptomatte;}
    void SetHasCryptomatte(bool b);
    void SetInstancerCryptoOffset(AtNode *node, size_t numInstances);

private:    
    HdArnoldRenderDelegate(const HdArnoldRenderDelegate&) = delete;
    HdArnoldRenderDelegate& operator=(const HdArnoldRenderDelegate&) = delete;

    void _SetRenderSetting(const TfToken& _key, const VtValue& value);

    void _ParseDelegateRenderProducts(const VtValue& value);

    void _ApplyLightLinking(AtNode* shape, const VtArray<TfToken>& categories);

    void _SetHasCryptomatte(bool b);

    /// Mutex for the shared Resource Registry.
    static std::mutex _mutexResourceRegistry;
    /// Atomic counter for the shared Resource Registry.
    static std::atomic_int _counterResourceRegistry;
    /// Pointer to the shared Resource Registry.
    static HdResourceRegistrySharedPtr _resourceRegistry;

    using LightLinkingMap = std::unordered_map<TfToken, std::vector<HdLight*>, TfToken::HashFunctor>;
    using NativeRprimTypeMap = std::unordered_map<TfToken, AtString, TfToken::HashFunctor>;
    using NativeRprimParams = std::unordered_map<AtString, NativeRprimParamList, AtStringHash>;
    
    using PathDependenciesMap = std::unordered_map<SdfPath, PathSet, SdfPath::Hash>;
    using DependencyChangesQueue = tbb::concurrent_queue<SdfPath>;

    struct SdfPathPairHash {
        std::size_t operator()(const std::pair<SdfPath, SdfPath>& paths) const
        {
            return TfHash::Combine(paths.first, paths.second);
        }
    };

    using DirtyBitsMap = std::unordered_map<std::pair<SdfPath, SdfPath>, HdDirtyBits, SdfPathPairHash>;

    // Every time we call TrackDependencies, we will store each of these changes
    // in a thread-safe queue. We need the source path, as well as each of its 
    // dependencies
    struct ArnoldDependencyChange {
        SdfPath source;
        PathSetWithDirtyBits targets;

        ArnoldDependencyChange() = default;

        ArnoldDependencyChange(const SdfPath& _source, const PathSetWithDirtyBits& _targets)
            : source(_source), targets(_targets)
        {
        }
    };
    using ArnoldDependencyChangesQueue = tbb::concurrent_queue<ArnoldDependencyChange>;

    HydraArnoldAPI _apiAdapter;
    TfTokenVector _renderTags; ///< List of current render tags.

    ArnoldDependencyChangesQueue _dependencyTrackQueue;    ///< Queue to track new dependencies for each source
    DependencyChangesQueue       _dependencyDirtyQueue;  ///< Queue to update the sources for a given dependency
    DependencyChangesQueue       _dependencyRemovalQueue;///< Queue to track dependencies removal events

    // We need to store maps in both directions, from sources to targets, 
    // and from targets to sources
    PathDependenciesMap _sourceToTargetsMap;       ///< Map to track dependencies between a source and its targets
    PathDependenciesMap _targetToSourcesMap;       ///< Map to track dependencies between a target and its sources   
    DirtyBitsMap _dependencyToDirtyBitsMap;  //< Map to track the dirty bits per dependencies

    using RenderTagTrackQueueElem = std::pair<AtNode*, TfToken>;
    /// Type to register shapes with render tags.
    using RenderTagTrackQueue = tbb::concurrent_queue<RenderTagTrackQueueElem>;
    /// Type to deregister shapes from the render tags map.
    using RenderTagUntrackQueue = tbb::concurrent_queue<AtNode*>;
    using RenderTagMap = std::unordered_map<AtNode*, TfToken>;
    RenderTagMap _renderTagMap;                   ///< Map to track render tags for each shape.
    RenderTagTrackQueue _renderTagTrackQueue;     ///< Queue to track shapes with render tags.
    RenderTagUntrackQueue _renderTagUntrackQueue; ///< Queue to untrack shapes from render tag map.

    std::mutex _lightLinkingMutex;                  ///< Mutex to lock all light linking operations.
    LightLinkingMap _lightLinks;                    ///< Light Link categories.
    LightLinkingMap _shadowLinks;                   ///< Shadow Link categories.
    std::atomic<bool> _lightLinkingChanged;         ///< Whether or not Light Linking have changed.
    DelegateRenderProducts _delegateRenderProducts; ///< Delegate Render Products for batch renders via husk.
    bool _delegateRenderProductsDirty = false;      ///< Flag to know if the arnold render products have been modified
    TfTokenVector _supportedRprimTypes;             ///< List of supported rprim types.
    NativeRprimTypeMap _nativeRprimTypes;           ///< Remapping between the native rprim type names and arnold types.
    NativeRprimParams _nativeRprimParams;           ///< List of parameters for native rprims.
    /// Pointer to an instance of HdArnoldRenderParam.
    ///
    /// This is shared with all the primitives, so they can control the flow of
    /// rendering.
    std::unique_ptr<HdArnoldRenderParam> _renderParam;
    SdfPath _id;           ///< Path of the Render Delegate.
    SdfPath _background;   ///< Path to the background shader.
    SdfPath _atmosphere;   ///< Path to the atmosphere shader.
    SdfPathVector _aov_shaders;  ///< Path to the aov shaders.
    SdfPath _imager;      ///< Path to the root imager node.
    SdfPath _subdiv_dicing_camera;  ///< Path to the subdiv dicing camera
    SdfPath _shader_override;  ///< Path to the shader_override material
    AtUniverse* _universe = nullptr; ///< Universe used by the Render Delegate.
    AtRenderSession* _renderSession = nullptr; ///< Render session used by the Render Delegate.
    AtNode* _options = nullptr;          ///< Pointer to the Arnold Options Node.
    AtNode* _fallbackShader = nullptr;   ///< Pointer to the fallback Arnold Shader.
    AtNode* _fallbackVolumeShader = nullptr; ///< Pointer to the fallback Arnold Volume Shader.
    AtNode* _procParent = nullptr;
    AtSessionMode _renderSessionType = AI_SESSION_INTERACTIVE;
    std::string _logFile;
    std::string _reportFile;
    std::string _statsFile;
    std::string _profileFile;
    AtString _pxrMtlxPath;

    std::mutex _meshLightsMutex;
    std::mutex _defaultShadersMutex;

    std::atomic<bool> _meshLightsChanged;
    std::set<AtNode*> _meshLights;

    /// FPS value from render settings.
    float _fps;
    // window used for overscan or to adjust the camera frustum
    GfVec4f _windowNDC = GfVec4f(0, 0, 1, 1);
    // resolution as returned from the render settings
    GfVec2i _resolution = GfVec2i(0, 0);
    float _pixelAspectRatio = 1.f;
    int _nodeId = 0;
    /// Top level render context using Hydra. Ie. Hydra, Solaris, Husk.
    TfToken _context;
    bool _isBatch = false; // are we in a batch rendering context (e.g. Husk)
    int _verbosityLogFlags = AI_LOG_WARNINGS | AI_LOG_ERRORS;
    bool _isArnoldActive = false;
    std::unordered_set<AtString, AtStringHash> _cryptomatteDrivers;
    std::string _outputOverride;
    int _mask = AI_NODE_ALL;  // mask for node types to be translated
    bool _hasCryptomatte = false;
    std::mutex _nodesMutex;
    mutable std::mutex _nodeNamesMutex;
    bool _renderDelegateOwnsUniverse;
    bool _enableNodesDestruction = true;
    bool _supportShapeInstancing = true;
    bool _forceIgnoreMotionBlur = false;
    std::unordered_map<std::string, AtNode *> _nodeNames;

    // We store a list of functions that must be run once all the prims are synced and have filled the 
    // vectors like 
    // They will be ran in the _Execute function
    std::mutex _deferredFunctionCallsMutex;
    std::vector<std::function<void()>> _deferredFunctionCalls;
    HydraArnoldReader *_reader = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE
