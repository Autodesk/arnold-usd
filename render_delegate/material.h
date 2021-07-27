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
/// @file render_delegate/material.h
///
/// Utilities for translating Hydra Materials for the Render Delegate.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/imaging/hd/material.h>

#include "render_delegate.h"

#include <ai.h>

#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for translating Hydra Materials to Arnold Materials.
class HdArnoldMaterial : public HdMaterial {
public:
    /// Constructor for HdArnoldMaterial.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the material.
    HDARNOLD_API
    HdArnoldMaterial(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

    /// Destructor for HdArnoldMaterial.
    ///
    /// Destory all Arnold Shader Nodes created.
    ~HdArnoldMaterial() override;

    /// Syncing the Hydra Material to the Arnold Shader Network.
    ///
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param renderPaaram Pointer to a HdArnoldRenderParam instance.
    /// @param dirtyBits Dirty Bits to sync.
    HDARNOLD_API
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;

    /// Returns the initial Dirty Bits for the Primitive.
    HDARNOLD_API
    HdDirtyBits GetInitialDirtyBitsMask() const override;

#if PXR_VERSION < 2011
    /// Reloads the shader.
    ///
    /// Note: this function is a pure virtual in USD up to 20.08, but removed after.
    ///
    /// Currently does nothing.
    HDARNOLD_API
    void Reload() override {}
#endif

    /// Returns the Entry Point to the Surface Shader Network.
    ///
    /// @return Pointer to the top Surface Shader.
    HDARNOLD_API
    AtNode* GetSurfaceShader() const;

    /// Returns the entry point to the Displacement Shader Network.
    ///
    /// @return Pointer to the top Displacement Shader.
    HDARNOLD_API
    AtNode* GetDisplacementShader() const;

    /// Returns the entry point to the Volume Shader Network.
    ///
    /// @return Pointer to the top Volume Shader.
    HDARNOLD_API
    AtNode* GetVolumeShader() const;

protected:
    /// Utility struct to store Translated Materials.
    struct MaterialData {
        /// Constructor for emplace functions.
        MaterialData(AtNode* _node, bool _updated) : node(_node), updated(_updated) {}
        /// Pointer to the Arnold Node.
        AtNode* node = nullptr;
        /// Boolean to store if the material has been updated or not.
        bool updated = false;
    };
    /// Utility struct to store the Arnold shader entries.
    struct ArnoldMaterial {
        /// Default constructor.
        ArnoldMaterial() = default;

        /// Constructor for emplace functions.
        ArnoldMaterial(AtNode* _surface, AtNode* _displacement, AtNode* _volume)
            : surface(_surface), displacement(_displacement), volume(_volume)
        {
        }

        /// Updates the material and tells if any of the terminals have changed.
        ///
        /// @param other Other material to use for the update.
        /// @param renderDelegate Pointer to the Arnold render delegate to access default shaders.
        /// @return True if any of the terminals have changed.
        bool UpdateMaterial(const ArnoldMaterial& other, HdArnoldRenderDelegate* renderDelegate)
        {
            const auto* oldSurface = surface;
            const auto* oldDisplacement = displacement;
            const auto* oldVolume = volume;
            surface = other.surface == nullptr ? renderDelegate->GetFallbackSurfaceShader() : other.surface;
            displacement = other.displacement;
            volume = other.volume == nullptr ? renderDelegate->GetFallbackVolumeShader() : other.volume;
            return oldSurface != surface || oldDisplacement != displacement || oldVolume != volume;
        }

        AtNode* surface = nullptr; ///< Surface entry to the material.
        AtNode* displacement = nullptr; ///< Displacement entry to the material.
        AtNode* volume = nullptr; ///< Volume entry to the material.
    };
    // We are using the new material network representation when available.
#ifdef USD_HAS_MATERIAL_NETWORK2
    /// Convert a Hydra Material Network 2 to an Arnold Shader Network.
    ///
    /// The newly created Arnold Nodes are stored in the class instance. Every
    /// previously created Arnold Node that's not touched is destroyed.
    ///
    /// @param network Const Reference to the Hydra Material Network.
    /// @param material Reference to the Arnold Material structure.
    /// @return Returns the Entry Point to the Arnold Shader Network.
    HDARNOLD_API
    void ReadMaterialNetwork(const HdMaterialNetwork2& network, ArnoldMaterial& material);

    /// Converts a Hydra Material to an Arnold Shader.
    ///
    /// The Arnold Node is stored in the class instance. Subsequent calls of a
    /// node with the same path do not translate nodes twice or create
    /// additional Arnold Nodes.
    ///
    /// @param material Const Reference to the Hydra Material Node.
    /// @return Pointer to the Arnold Node.
    HDARNOLD_API
    AtNode* ReadMaterialNode(const HdMaterialNode2& node);
#else
    /// Convert a Hydra Material Network to an Arnold Shader Network.
    ///
    /// The newly created Arnold Nodes are stored in the class instance. Every
    /// previously created Arnold Node that's not touched is destroyed.
    ///
    /// @param network Const Reference to the Hydra Material Network.
    /// @return Returns the Entry Point to the Arnold Shader Network.
    HDARNOLD_API
    AtNode* ReadMaterialNetwork(const HdMaterialNetwork& network);

    /// Converts a Hydra Material to an Arnold Shader.
    ///
    /// The Arnold Node is stored in the class instance. Subsequent calls of a
    /// node with the same path do not translate nodes twice or create
    /// additional Arnold Nodes.
    ///
    /// @param node Const Reference to the Hydra Material Node.
    /// @return Pointer to the Arnold Node.
    HDARNOLD_API
    AtNode* ReadMaterialNode(const HdMaterialNode& node);
#endif

    /// Looks up a Material in the internal Arnold Node storage.
    ///
    /// @param id Path to the Hydra Material Node.
    /// @return Pointer to the Material Data representing the Hydra Material
    ///  Node if the Hydra Material Node was already translated, nullptr otherwise.
    HDARNOLD_API
    AtNode* FindMaterial(const SdfPath& id) const;

    /// Returns a local node name prefixed by the Material's path.
    ///
    /// @param path Path to be prefixed.
    /// @return AtString that holds the path prefixed with the Material's path.
    HDARNOLD_API
    AtString GetLocalNodeName(const SdfPath& path) const;

    /// Returns a local node based on the path and the node type.
    ///
    /// Creates a new node if the node can't be found with the given name or
    /// it's not the right type. Returns the existing node if type and name
    /// matches, nullptr if there is an error. It marks the node updated upon
    /// successful return value. Existing materials are reset upon return.
    ///
    /// @param path Path to the node.
    /// @param nodeType Type of the node.
    /// @return Pointer to the node, nullptr if there was an error.
    HDARNOLD_API
    AtNode* GetLocalNode(const SdfPath& path, const AtString& nodeType);

    /// Clears all nodes that are not updated during sync.
    ///
    /// Confirms if the entry point is valid and updated, otherwise it prints
    /// a coding error.
    ///
    /// @param material Entry points to the shader network.
    /// @return True if all entry points were translated, false otherwise.
    HDARNOLD_API
    bool ClearUnusedNodes(const ArnoldMaterial& material);

    /// Sets all shader nodes unused.
    HDARNOLD_API
    void SetNodesUnused();

    /// Storage for nodes created by HdArnoldMaterial.
    std::unordered_map<SdfPath, MaterialData, SdfPath::Hash> _nodes;
    HdArnoldRenderDelegate* _renderDelegate; ///< Pointer to the Render Delegate.
    /// Storing arnold shader entry points.
    ArnoldMaterial _material;
    bool _wasSyncedOnce = false; ///< Whether or not the material has been synced at least once.
};

PXR_NAMESPACE_CLOSE_SCOPE
