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
// Modifications Copyright 2019 Autodesk, Inc.
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

#include <ai.h>

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
};

/// Render context for the render delegate.
enum class HdArnoldRenderContext {
    Hydra, ///< Generic Hydra renderer.
    Husk,  ///< Husk from Houdini.
};

/// Main class point for the Arnold Render Delegate.
class HdArnoldRenderDelegate final : public HdRenderDelegate {
public:
    HDARNOLD_API
    HdArnoldRenderDelegate(
        HdArnoldRenderContext context = HdArnoldRenderContext::Hydra); ///< Constructor for the Render Delegate.
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
#if PXR_VERSION >= 2102
    /// Request to create a new instancer.
    ///
    /// @param id The unique identifier of this instancer.
    /// @return A pointer to the new instancer or nullptr on error.
    HdInstancer* CreateInstancer(HdSceneDelegate* delegate, SdfPath const& id) override;
#else
    /// Creates a new Point Instancer.
    ///
    /// @param delegate Pointer to the Scene Delegate.
    /// @param id Path to the Point Instancer.
    /// @param instancerId Path to the parent Point Instancer.
    /// @return Pointer to a new Point Instancer or nullptr on error.
    HDARNOLD_API
    HdInstancer* CreateInstancer(HdSceneDelegate* delegate, SdfPath const& id, SdfPath const& instancerId) override;
#endif
    /// Destroys a Point Instancer.
    ///
    /// @param instancer Pointer to an instance of HdInstancer.
    HDARNOLD_API
    void DestroyInstancer(HdInstancer* instancer) override;
#if PXR_VERSION >= 2102
    /// Creates a new RPrim.
    ///
    /// @param typeId Type name of the primitive.
    /// @param rprimId Path to the primitive.
    /// @return Pointer to the newly created RPrim or nullptr on error.
    HDARNOLD_API
    HdRprim* CreateRprim(TfToken const& typeId, SdfPath const& rprimId) override;
#else
    /// Creates a new RPrim.
    ///
    /// @param typeId Type name of the primitive.
    /// @param rprimId Path to the primitive.
    /// @param instancerId Path to the parent Point Instancer.
    /// @return Pointer to the newly created RPrim or nullptr on error.
    HDARNOLD_API
    HdRprim* CreateRprim(TfToken const& typeId, SdfPath const& rprimId, SdfPath const& instancerId) override;
#endif
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
#if PXR_VERSION >= 2105
    /// Returns a list, in decending order of preference, that can be used to
    /// select among multiple material network implementations. The default
    /// list contains an empty token.
    ///
    /// Since USD 21.05 GetMaterialNetworkSelector is deprecated.
    ///
    /// @return List of material render contexts.
    HDARNOLD_API
    TfTokenVector GetMaterialRenderContexts() const override;
#else
    /// Returns a token to indicate which material network should be preferred.
    ///
    /// The function currently returns "arnold", to indicate using
    /// outputs:arnold:surface over outputs:surface (and displacement/volume)
    /// if avialable.
    ///
    /// @return Name of the preferred material network.
    HDARNOLD_API
    TfToken GetMaterialNetworkSelector() const override;
#endif
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
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    /// Gets the active Arnold Render Session.
    ///
    /// @return Pointer to the Render Session used by the Render Delegate.
    HDARNOLD_API
    AtRenderSession* GetRenderSession() const;
#endif
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
    AtNode* GetFallbackSurfaceShader() const;
    /// Gets fallback Arnold Volume shader.
    ///
    /// The fallback shader is just an instances of standard_volume.
    ///
    /// @return Pointer to the fallback Arnold Volume Shader.
    HDARNOLD_API
    AtNode* GetFallbackVolumeShader() const;
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

    /// Apply light linking to a shape.
    ///
    /// @param shape Pointer to the Arnold Shape.
    /// @param categories List of categories the shape belongs to.
    HDARNOLD_API
    void ApplyLightLinking(AtNode* shape, const VtArray<TfToken>& categories);

    /// Tells whether or not the current convergence iteration should be skipped.
    ///
    /// This function can be used to skip calling the render function in HdRenderPass, so a sync step will be enforced
    /// before the next iteration.
    ///
    /// @param renderIndex Pointer to the Hydra Render Index.
    /// @param shutterOpen Shutter Open value of the active camera.
    /// @param shutterClose Shutter Close value of the active camera.
    /// @return True if the iteration should be skipped.
    HDARNOLD_API
    bool ShouldSkipIteration(HdRenderIndex* renderIndex, const GfVec2f& shutter);

    using DelegateRenderProducts = std::vector<HdArnoldDelegateRenderProduct>;
    /// Returns the list of available Delegate Render Products.
    ///
    /// @return Const Reference to the list of Delegate Render Products.
    const DelegateRenderProducts& GetDelegateRenderProducts() const { return _delegateRenderProducts; }
    /// Advertise whether this delegate supports pausing and resuming of
    /// background render threads. Default implementation returns false.
    ///
    /// @return True if pause/restart is supported.
    HDARNOLD_API
    bool IsPauseSupported() const override;
    /// Pause all of this delegate's background rendering threads. Default
    /// implementation does nothing.
    ///
    /// @return True if successful.
    HDARNOLD_API
    bool Pause() override;
    /// Resume all of this delegate's background rendering threads previously
    /// paused by a call to Pause. Default implementation does nothing.
    ///
    /// @return True if successful.
    HDARNOLD_API
    bool Resume() override;

#if PXR_VERSION >= 2011
    using NativeRprimParamList = std::unordered_map<TfToken, const AtParamEntry*, TfToken::HashFunctor>;
#else
    using NativeRprimParamList = std::vector<std::pair<TfToken, const AtParamEntry*> >;
#endif

    /// Returns a list of parameters for each native rprim.
    ///
    /// @param arnoldNodeType Type of the arnold node.
    /// @return Constant Pointer to the list of parameters for each native rprim.
    HDARNOLD_API
    const NativeRprimParamList* GetNativeRprimParamList(const AtString& arnoldNodeType) const;

    using PathSet = std::unordered_set<SdfPath, SdfPath::Hash>;

    /// Track dependencies from one prim to others
    ///
    /// @param shape Id of the prim to track.
    /// @param targets list of dependencies for a given source. This will override eventually existing ones.
    HDARNOLD_API
    void TrackDependencies(const SdfPath& source, const PathSet& targets);

    /// Untrack all dependencies assigned to a source
    ///
    /// @param source Id of the source prim
    void ClearDependencies(const SdfPath& source) {TrackDependencies(source, PathSet());}

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

    // Store the list of cryptomatte driver names, so that we can get the cryptomatte
    // metadatas in their attribute "custom_attributes"
    /// @param driver Name of a driver used for a cryptomatte AOVs (crypto_material, crypto_asset, crypto_object)
    HDARNOLD_API
    void RegisterCryptomatteDriver(const AtString& driver);

    // Clear the list of cryptomatte driver names, before outputs are setup
    HDARNOLD_API
    void ClearCryptomatteDrivers();

private:
    HdArnoldRenderDelegate(const HdArnoldRenderDelegate&) = delete;
    HdArnoldRenderDelegate& operator=(const HdArnoldRenderDelegate&) = delete;

    void _SetRenderSetting(const TfToken& _key, const VtValue& value);

    void _ParseDelegateRenderProducts(const VtValue& value);

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

    // Every time we call TrackDependencies, we will store each of these changes
    // in a thread-safe queue. We need the source path, as well as each of its 
    // dependencies
    struct ArnoldDependencyChange {
        SdfPath source;
        PathSet targets;

        ArnoldDependencyChange() = default;

        ArnoldDependencyChange(const SdfPath& _source, const PathSet& _targets)
            : source(_source), targets(_targets)
        {
        }
    };
    using ArnoldDependencyChangesQueue = tbb::concurrent_queue<ArnoldDependencyChange>;

    TfTokenVector _renderTags; ///< List of current render tags.

    ArnoldDependencyChangesQueue _dependencyTrackQueue;    ///< Queue to track new dependencies for each source
    DependencyChangesQueue       _dependencyDirtyQueue;  ///< Queue to update the sources for a given dependency
    DependencyChangesQueue       _dependencyRemovalQueue;///< Queue to track dependencies removal events

    // We need to store maps in both directions, from sources to targets, 
    // and from targets to sources
    PathDependenciesMap _sourceToTargetsMap;       ///< Map to track dependencies between a source and its targets
    PathDependenciesMap _targetToSourcesMap;       ///< Map to track dependencies between a target and its sources   

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
    SdfPath _aov_shaders;  ///< Path to the aov shaders.
    SdfPath _subdiv_dicing_camera;  ///< Path to the subdiv dicing camera
    AtUniverse* _universe; ///< Universe used by the Render Delegate.
#ifdef ARNOLD_MULTIPLE_RENDER_SESSIONS
    AtRenderSession* _renderSession; ///< Render session used by the Render Delegate.
#endif
    AtNode* _options;              ///< Pointer to the Arnold Options Node.
    AtNode* _fallbackShader;       ///< Pointer to the fallback Arnold Shader.
    AtNode* _fallbackVolumeShader; ///< Pointer to the fallback Arnold Volume Shader.
    std::string _logFile;
    /// FPS value from render settings.
    float _fps;
    /// Top level render context using Hydra. Ie. Hydra, Solaris, Husk.
    HdArnoldRenderContext _context = HdArnoldRenderContext::Hydra;
    int _verbosityLogFlags = AI_LOG_WARNINGS | AI_LOG_ERRORS;
    bool _ignoreVerbosityLogFlags = false;
    bool _isArnoldActive = false;
    std::unordered_set<AtString, AtStringHash> _cryptomatteDrivers;
};

PXR_NAMESPACE_CLOSE_SCOPE
