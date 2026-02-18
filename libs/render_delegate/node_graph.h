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
#include <pxr/imaging/hd/material.h>

#include "api.h"
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
    ~HdArnoldNodeGraph() override;

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

    /// Returns the Entry Point to the Surface Shader Network.
    ///
    /// @return Pointer to the top Surface Shader.
    HDARNOLD_API
    AtNode* GetCachedSurfaceShader() const;

    /// Returns the entry point to the Displacement Shader Network.
    ///
    /// @return Pointer to the top Displacement Shader.
    HDARNOLD_API
    AtNode* GetCachedDisplacementShader() const;

    /// Returns the entry point to the Volume Shader Network.
    ///
    /// @return Pointer to the top Volume Shader.
    HDARNOLD_API
    AtNode* GetCachedVolumeShader() const;

    /// Returns a custom terminal.
    ///
    /// @param terminalName Name of the terminal to lookup.
    /// @return Pointer to the terminal, nullptr if not found.
    HDARNOLD_API
    AtNode* GetCachedTerminal(const TfToken& terminalName) const;

    /// Returns a custom terminal.
    ///
    /// @param terminalBase Name of the terminal to lookup.
    /// @return Vector of pointers to the terminal, nullptr if not found.
    HDARNOLD_API
    std::vector<AtNode*> GetCachedTerminals(const TfToken& terminalBase);

    /// Returns a custom terminal.
    ///
    /// @param terminalName Name of the terminal to lookup.
    /// @return Pointer to the terminal, nullptr if not found.
    HDARNOLD_API
    AtNode* GetOrCreateTerminal(HdSceneDelegate* sceneDelegate, const TfToken& terminalName);

    /// Returns a custom terminal.
    ///
    /// @param terminalBase Name of the terminal to lookup.
    /// @return Vector of pointers to the terminal, nullptr if not found.
    HDARNOLD_API
    std::vector<AtNode*> GetOrCreateTerminals(HdSceneDelegate* sceneDelegate, const TfToken& terminalBase);


    /// Helper static function that returns the node graph for a given path
    ///
    /// @param renderIndex  Pointer to the Hydra render index
    /// @param id  Path of the node graph primitive
    /// @return Pointer to the requested HdArnoldNodeGraph 
    HDARNOLD_API
    static HdArnoldNodeGraph* GetNodeGraph(HdRenderIndex* renderIndex, const SdfPath& id);

    HDARNOLD_API
    static HdArnoldNodeGraph* GetNodeGraph(HdRenderIndex &renderIndex, const SdfPath& id);

    /// Create an Arnold shader node for this node graph. 
    /// We need to store the list of shaders created for this node graph,
    /// so that they can be properly deleted later on
    ///
    /// @param nodeType  Arnold node type to create
    /// @param nodeName  Name of the Arnold node to create
    /// @return Pointer to the created Arnold node
    HDARNOLD_API
    AtNode* CreateArnoldNode(const char* nodeType, const char* nodeName)
    {
        // If this node was already in our list for the previous iteration, 
        // we want to clear it from the previousNodes list,
        // so that we don't delete it after the node graph is translated.
        if (!_previousNodes.empty()) {
            auto previousNodeIt = _previousNodes.find(nodeName);
            if (previousNodeIt != _previousNodes.end())
                previousNodeIt->second = nullptr;
        }

        // Check if we already have an Arnold node for this name
        auto registeredNodeIt = _nodes.find(nodeName);
        if (registeredNodeIt != _nodes.end()) {
            // An existing node was found
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
        // Ask the render delegate to create an arnold node with the expected type and name
        AtNode* node = _renderDelegate->CreateArnoldNode(AtString(nodeType), AtString(nodeName));
        // Store this node in our local list
        _nodes[nodeName] = node;
        return node;
    }    


    /// Notify this graph that it is an imager graph, which requires a different
    /// way to update the render
    HDARNOLD_API
    void SetImagerGraph(bool b) {_imagerGraph = b;}
    
protected:

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
        bool UpdateTerminal(const TfToken& terminalName, AtNode* terminal, AtNode*& oldTerminal)
        {
            // TODO if a node changes and it was stored in a terminal, 
            // it needs to be removed from this list
            auto it = std::find_if(terminals.begin(), terminals.end(), [&terminalName](const Terminal& t) -> bool {
                return t.first == terminalName;
            });
            if (it == terminals.end()) {
                terminals.push_back({terminalName, terminal});
                return true;
            } else {
                oldTerminal = it->second;
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

        /// Returns true if a terminal node with terminalName is found in the cache 
        ///
        /// @param terminalName Name of the terminal.
        /// @return false if terminal does not exists.
        bool HasTerminal(const TfToken& terminalName) const
        {
            return std::find_if(terminals.begin(), terminals.end(), [&terminalName](const Terminal& t) -> bool {
                       return t.first == terminalName;
                   }) != terminals.end();
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
    /// @param terminalType Type of the shading network (surface, displacement, volume, etc...)
    /// @param terminals Reference of a list of terminals root nodes, where elements can be removed inside the call
    /// @return Returns the Entry Point to the Arnold Shader Network.
    HDARNOLD_API
    AtNode* ReadMaterialNetwork(const HdMaterialNetwork& network, const TfToken& terminalType, 
        std::vector<SdfPath>& terminals);

    ArnoldNodeGraph _nodeGraphCache;         ///< Storing arnold shaders for terminals.
    HdArnoldRenderDelegate* _renderDelegate; ///< Pointer to the Render Delegate.
    bool _wasSyncedOnce = false;             ///< Whether or not the material has been synced at least once.
    bool _imagerGraph = false;
    std::unordered_map<std::string, AtNode*> _nodes;  /// List of nodes used in this translator
    std::unordered_map<std::string, AtNode*> _previousNodes;  /// Transient list of previously stored nodes
};

PXR_NAMESPACE_CLOSE_SCOPE
