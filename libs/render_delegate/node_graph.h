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
/// @file render_delegate/node_graph.h
///
/// Utilities for translating Hydra Materials and Node Graphs for the Render Delegate.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/imaging/hd/material.h>

#include <constant_strings.h>

#include "render_delegate.h"

#include <ai.h>

#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for translating Hydra Node Graphs to Arnold nodes.
class HdArnoldNodeGraph : public HdMaterial {
public:
    /// Constructor for HdArnoldNodeGraph.
    ///
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the material.
    HDARNOLD_API
    HdArnoldNodeGraph(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

    /// Destructor for HdArnoldNodeGraph.
    ///
    /// Destory all Arnold Shader Nodes created.
    ~HdArnoldNodeGraph() override = default;

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

    /// Returns a custom terminal.
    ///
    /// @param terminalName Name of the terminal to lookup.
    /// @return Pointer to the terminal, nullptr if not found.
    HDARNOLD_API
    AtNode* GetTerminal(const TfToken& terminalName) const;

    /// Returns a custom terminal.
    ///
    /// @param terminalBase Name of the terminal to lookup.
    /// @return Vector of pointers to the terminal, nullptr if not found.
    HDARNOLD_API
    std::vector<AtNode*> GetTerminals(const TfToken& terminalBase) const;

    /// Helper static function that returns the node graph for a given path
    ///
    /// @param renderIndex  Pointer to the Hydra render index
    /// @param id  Path of the node graph primitive
    /// @return Pointer to the requested HdArnoldNodeGraph 
    HDARNOLD_API
    static const HdArnoldNodeGraph* GetNodeGraph(HdRenderIndex* renderIndex, const SdfPath& id);

    AtNode* CreateArnoldNode(const char* nodeType, const char* nodeName)
    {
        auto& registeredNodeIt = _nodes.find(nodeName);
        if (registeredNodeIt != _nodes.end()) {
            // An existing node was found with the same name
            AtNode* node = registeredNodeIt->second;
            if (node) {
                // Compare the node type to ensure we don't reuse an incompatible shader
                if (strcmp(nodeType, AiNodeEntryGetNameAtString(AiNodeGetNodeEntry(node))) == 0) {
                    // We already had a node for this name with the same node type, 
                    // we can just return it. First we reset it so that its previous 
                    // attributes and connections are clean.
                    AiNodeReset(node);
                    return node;
                }
                // The previous node had a different node type. We need to delete it.
                _renderDelegate->DestroyArnoldNode(node);
            }
        }
        AtNode* node = _renderDelegate->CreateArnoldNode(AtString(nodeType), AtString(nodeName));
        _nodes[nodeName] = node;
        return node;
    }    
protected:

    /*
    /// Utility struct to store translated nodes.
    struct NodeData {
        /// Constructor for emplace functions.
        NodeData(AtNode* _node, bool _used, HdArnoldRenderDelegate *_renderDelegate) : 
            node(_node), used(_used), renderDelegate(_renderDelegate) {}
        /// Destructor.
        ~NodeData()
        {
            renderDelegate->DestroyArnoldNode(node);
        }
        /// Pointer to the Arnold Node.
        AtNode* node = nullptr;
        /// Boolean to store if the material has been used or not.
        bool used = false;
        HdArnoldRenderDelegate *renderDelegate;
    };
    using NodeDataPtr = std::shared_ptr<NodeData>;

    */


    using ConnectedInputs = std::unordered_map<SdfPath, std::vector<const HdMaterialRelationship*>, TfHash>;
    /// Utility struct to store the Arnold shader entries.
    struct ArnoldNodeGraph {
        /// Default constructor.
        ArnoldNodeGraph() = default;

        /// Update the terminal and return true if the terminal has changed.
        ///
        /// @param terminalName Name of the terminal.
        /// @param terminal Arnold node at the terminal.
        /// @return True if the terminal has changed, false otherwise.
        bool UpdateTerminal(const TfToken& terminalName, AtNode* terminal)
        {
            auto it = std::find_if(terminals.begin(), terminals.end(), [&terminalName](const Terminal& t) -> bool {
                return t.first == terminalName;
            });
            if (it == terminals.end()) {
                terminals.push_back({terminalName, terminal});
                return true;
            } else {
                auto* oldTerminal = it->second;
                it->second = terminal;
                return oldTerminal != terminal;
            }
        }

        /// Returns a terminal of the nodegraph.
        ///
        /// @param terminalName Name of the terminal.
        /// @return Pointer to the terminal, nullptr if terminal does not exists.
        AtNode* GetTerminal(const TfToken& terminalName) const
        {
            auto it = std::find_if(terminals.begin(), terminals.end(), [&terminalName](const Terminal& t) -> bool {
                return t.first == terminalName;
            });
            return it == terminals.end() ? nullptr : it->second;
        }

        /// Returns a terminal of the nodegraph.
        ///
        /// @param terminalName Name of the terminal.
        /// @return Pointer to the terminal, nullptr if terminal does not exists.
        std::vector<AtNode*> GetTerminals(const TfToken& terminalBase) const
        {
            std::vector<AtNode*> result;
            for (auto& t: terminals)
                if (t.first.GetString().rfind(terminalBase.GetString(), 0) == 0)
                    result.push_back(t.second);
            return result;
        }

        /// Checks if the shader any of the terminals.
        ///
        /// @param terminal Pointer to the Arnold node.
        /// @return True if the Arnold node is one of the terminals, false otherwise.
        bool ContainsTerminal(const AtNode* terminal)
        {
            return std::find_if(terminals.begin(), terminals.end(), [&terminal](const Terminal& t) -> bool {
                       return t.second == terminal;
                   }) != terminals.end();
        }

        using Terminal = std::pair<TfToken, AtNode*>;
        using Terminals = std::vector<Terminal>;
        Terminals terminals; ///< Terminal entries to the node graph.
    };
    /// Convert a Hydra Material Network to an Arnold Shader Network.
    ///
    /// The newly created Arnold Nodes are stored in the class instance. Every
    /// previously created Arnold Node that's not touched is destroyed.
    ///
    /// @param network Const Reference to the Hydra Material Network.
    /// @return Returns the Entry Point to the Arnold Shader Network.
    HDARNOLD_API
    AtNode* ReadMaterialNetwork(const HdMaterialNetwork& network, bool isDisplacement, 
        std::vector<SdfPath>& terminals);
    
/*

    /// Converts a Hydra Material to an Arnold Shader.
    ///
    /// The Arnold Node is stored in the class instance. Subsequent calls of a
    /// node with the same path do not translate nodes twice or create
    /// additional Arnold Nodes.
    ///
    /// @param node Const Reference to the Hydra Material Node.
    /// @return Pointer to the Arnold Node.
    HDARNOLD_API
    AtNode* ReadMaterialNode(const HdMaterialNode& node, const ConnectedInputs &);

    /// Looks up a shader in the internal Arnold node storage.
    ///
    /// @param id Path to the Hydra material node.
    /// @return Pointer to the Arnold node translated from the Hydra material node.
    ///  Node if the Hydra Material Node was already translated, nullptr otherwise.
    HDARNOLD_API
    AtNode* FindNode(const SdfPath& id) const;

    /// Returns a local shader name prefixed by the Material's path.
    ///
    /// @param path Path to be prefixed.
    /// @return AtString that holds the path prefixed with the Material's path.
    HDARNOLD_API
    AtString GetLocalNodeName(const SdfPath& path) const;

    /// Returns a local node based on the path and the node type.
    ///
    /// Creates a new node if the node can't be found with the given name or
    /// it's not the right type. Returns the existing node if type and name
    /// matches, nullptr if there is an error. It marks the node used upon
    /// successful return value. Existing materials are reset upon return.
    ///
    /// @param path Path to the node.
    /// @param nodeType Type of the node.
    /// @param con List of connected input attributes, needed for materialx
    /// @param isMaterialx returned value will be true is this node represents a materialx description
    /// @return Pointer to the node, nullptr if there was an error.
    HDARNOLD_API
    NodeDataPtr GetNode(const SdfPath& path, const AtString& nodeType, 
                        const ConnectedInputs &con, bool &isMaterialx);

    /// Clears all nodes that are not used during sync.
    ///
    /// Confirms if the entry point is valid and used, otherwise it prints
    /// a coding error.
    ///
    /// @return True if all entry points were translated, false otherwise.
    HDARNOLD_API
    bool ClearUnusedNodes();

    /// Sets all shader nodes unused.
    HDARNOLD_API
    void SetNodesUnused();
*/
    
    /// Storage for nodes created by HdArnoldNodeGraph.
//    std::unordered_map<SdfPath, std::shared_ptr<NodeData>, SdfPath::Hash> _nodes;

    ArnoldNodeGraph _nodeGraph;              ///< Storing arnold shaders for terminals.
    HdArnoldRenderDelegate* _renderDelegate; ///< Pointer to the Render Delegate.
    bool _wasSyncedOnce = false;             ///< Whether or not the material has been synced at least once.
    std::unordered_map<std::string, AtNode*> _nodes;
};

PXR_NAMESPACE_CLOSE_SCOPE
