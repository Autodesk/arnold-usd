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
/// @file material.h
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
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the material.
    HDARNOLD_API
    HdArnoldMaterial(HdArnoldRenderDelegate* delegate, const SdfPath& id);

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

    /// Reloads the shader.
    ///
    /// Currently does nothing.
    HDARNOLD_API
    void Reload() override;

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
    /// @param material Const Reference to the Hydra Material Node.
    /// @return Pointer to the Arnold Node.
    HDARNOLD_API
    AtNode* ReadMaterial(const HdMaterialNode& material);

    /// Converts a Material definition from Katana 3.2 to an Arnold Shader.
    ///
    /// Katana 3.2 returns shaders in a special way, returning shader code,
    /// rather than a HdMaterialNetwork, so we are remapping the shader code
    /// to standard surface nodes.
    ///
    /// @param sceneDelegate Pointer to the HdSceneDelegate.
    /// @param id SdfPath to the material.
    /// @return Pointer to the Arnold Node or nullptr if conversion was
    ///  unsuccessful.
    HDARNOLD_API
    AtNode* ReadKatana32Material(HdSceneDelegate* sceneDelegate, const SdfPath& id);

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
    /// @param entryPoint Point to the entry to the shader network.
    /// @return True if entry point was translated, false otherwise.
    HDARNOLD_API
    bool ClearUnusedNodes(
        const AtNode* surfaceEntryPoint = nullptr, const AtNode* displacementEntryPoint = nullptr,
        const AtNode* volumeEntryPoint = nullptr);

    /// Sets all shader nodes unused.
    HDARNOLD_API
    void SetNodesUnused();

    /// Storage for nodes created by HdArnoldMaterial.
    std::unordered_map<SdfPath, MaterialData, SdfPath::Hash> _nodes;
    HdArnoldRenderDelegate* _delegate; ///< Pointer to the Render Delegate.
    /// Pointer to the entry point to the Surface Shader Network.
    AtNode* _surface = nullptr;
    /// Pointer to the entry point to the Displacement Shader Network.
    AtNode* _displacement = nullptr;
    /// Pointer to the entry point to the Volume Shader Network.
    AtNode* _volume = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE
