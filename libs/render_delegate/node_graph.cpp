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
#include "node_graph.h"

#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#include <constant_strings.h>
#include "debug_codes.h"
#include "hdarnold.h"
#include "utils.h"

#include <ai.h>
#include <iostream>
#include <unordered_map>
#include <materials_utils.h>


PXR_NAMESPACE_OPEN_SCOPE

// MaterialReader classes are shared between the procedural and delegate code
// to hold information needed to translate a shading tree.
class MaterialHydraReader : public MaterialReader
{
public:
    MaterialHydraReader(HdArnoldNodeGraph& nodeGraph, 
                    const HdMaterialNetwork& network,
                    HydraArnoldAPI& context) : 
                    MaterialReader(),
                    _nodeGraph(nodeGraph),
                    _network(network),
                    _context(context)
                    {}


    AtNode* CreateArnoldNode(const char* nodeType, const char* nodeName) override 
    {        
        return _nodeGraph.CreateArnoldNode(nodeType, nodeName);
    }

    void ConnectShader(AtNode* node, const std::string& attrName, 
            const SdfPath& target, ArnoldAPIAdapter::ConnectionType type) override 
    {
        if (target.HasPrefix(_nodeGraph.GetId())) {
            _context.AddConnection(
                node, attrName.c_str(), target.GetPrimPath().GetText(),
                type, target.GetElementString());
            
        }
        else {
            // If the connected shader is not already prefixed with our material path,
            // we add this prefix to that shader name #1940        
            std::string targetPath = 
                _nodeGraph.GetId().GetString() + target.GetPrimPath().GetString();
            _context.AddConnection(
                node, attrName.c_str(), targetPath.c_str(),
                type, target.GetElementString());    
        }
        
    }
    
    // GetShaderInput is called to return a parameter value for a given shader
    // in the current network. It also returns the shaderId of the shader
    bool GetShaderInput(const SdfPath& shaderPath, const TfToken& param,
        VtValue& value, TfToken& shaderId) override
    {
        for (const auto& node : _network.nodes) {
            if (node.path != shaderPath) 
                continue;

            // found a node with the same name, let's store its shadeId
            shaderId = node.identifier;
            // search in its attributes for a parameter of the given name;
            for (const auto& paramIt : node.parameters) {
                if (paramIt.first != param)
                    continue;
                // found the expected attribute, let's return its value
                value = paramIt.second;
                // return true if there is an actual value 
                // (should be the case at this stage)
                return (!value.IsEmpty());
            }
            // We didn't find this attribute
            return false;
        }
        // We didn't find this node
        return false;
    }

private:
    HdArnoldNodeGraph& _nodeGraph;
    const HdMaterialNetwork& _network;
    HydraArnoldAPI& _context;
};

HdArnoldNodeGraph::HdArnoldNodeGraph(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdMaterial(id), _renderDelegate(renderDelegate)
{
}

HdArnoldNodeGraph::~HdArnoldNodeGraph()
{
    // We need to clear the external dependencies on the Material, it happens when the Material has
    // a camera_projection shader connected to a camera.
    _renderDelegate->ClearDependencies(GetId());

    // Ensure all AtNodes created for this node graph are properly deleted
    for (const auto& node : _nodes) {
        if (node.second)
            AiNodeDestroy(node.second);
    }

}

// Root function called to translate a shading NodeGraph primitive
void HdArnoldNodeGraph::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{    
    if (!_renderDelegate->CanUpdateScene())
        return;
 
    const auto id = GetId();
    if ((*dirtyBits & HdMaterial::DirtyResource) && !id.IsEmpty()) {
        HdArnoldRenderParamInterrupt param(renderParam);
        const VtValue value = sceneDelegate->GetMaterialResource(GetId());
        bool nodeGraphChanged = false;

        if (value.IsHolding<HdMaterialNetworkMap>()) {
            param.Interrupt();
            const HdMaterialNetworkMap& map = value.UncheckedGet<HdMaterialNetworkMap>();
            // Before translation starts, we store the previous list of AtNodes
            // for this NodeGraph. After we translated everything, all unused nodes
            // in this list will be destroyed
            _previousNodes = _nodes;

            // terminals contains the list of terminal node paths
            // whether it's for displacement, surface, volume, etc...
            // As we'll use this to identify the networks root shaders, 
            // we copy the vector and we'll remove elements as we find them.
            // Note that this vector should only have a single or a few elements.
            std::vector<SdfPath> terminals = map.terminals;
            for (const auto& terminal : map.map) {
                // terminalType tells us which type of network this is meant to be
                // (surface, displacement, etc...). We're using it to identify a 
                // special case for displacement with UsdPreviewSurface
                const TfToken& terminalType = terminal.first;

                // network will contain the list of shaders nodes to translate, 
                // as well as the list of relationships (shader connections)
                const HdMaterialNetwork& network = terminal.second;
                // If this network doesn't contain any node, then there's nothing to do
                if (network.nodes.empty())
                    continue;
                // Read the material network and retrieve the "root" shader that will referenced
                // from other nodes through one of our terminals. 
                AtNode* node = ReadMaterialNetwork(network, terminalType, terminals);
                // UpdateTerminal assigns a given shader to a terminal name
                if (node && _nodeGraph.UpdateTerminal(
                        terminal.first, node)) {
                    nodeGraphChanged = true;
                }
                
                // Special case for light filters, we need to flush the cache to ensure
                // they're properly updated in Arnold
                if (terminalType == str::color || terminalType.GetString().rfind(
                        "light_filter", 0) == 0) {
                    nodeGraphChanged = true;
                    AiUniverseCacheFlush(_renderDelegate->GetUniverse(), AI_CACHE_BACKGROUND);
                }
            }
            // Loop through previous AtNodes that were created for this node graph.
            // If they're not empty in this list, it means that they're not used anymore.
            // Let's delete the unused ones
            for (const auto& previousNode : _previousNodes) {
                if (previousNode.second) {
                    // Destroy the arnold node
                    AiNodeDestroy(previousNode.second);
                    // Remove this pointer from our list of nodes
                    auto it = _nodes.find(previousNode.first);
                    if (it != _nodes.end())
                        _nodes.erase(it);

                }
            }
            _previousNodes.clear();
        }
        // We only mark the material dirty if one of the terminals have changed, but ignore the initial sync, because we
        // expect Hydra to do the initial assignment correctly.
        if (_wasSyncedOnce && nodeGraphChanged) {
            _renderDelegate->DirtyDependency(id);
        }
    }
    *dirtyBits = HdMaterial::Clean;
    _wasSyncedOnce = true;
}

HdDirtyBits HdArnoldNodeGraph::GetInitialDirtyBitsMask() const { return HdMaterial::DirtyResource; }

AtNode* HdArnoldNodeGraph::GetSurfaceShader() const
{
    auto* terminal = _nodeGraph.GetTerminal(HdMaterialTerminalTokens->surface);
    return terminal == nullptr ? _renderDelegate->GetFallbackSurfaceShader() : terminal;
}

AtNode* HdArnoldNodeGraph::GetDisplacementShader() const { return _nodeGraph.GetTerminal(str::t_displacement); }

AtNode* HdArnoldNodeGraph::GetVolumeShader() const
{
    auto* terminal = _nodeGraph.GetTerminal(HdMaterialTerminalTokens->volume);
    return terminal == nullptr ? _renderDelegate->GetFallbackVolumeShader() : terminal;
}

AtNode* HdArnoldNodeGraph::GetTerminal(const TfToken& terminalName) const
{
    return _nodeGraph.GetTerminal(terminalName);
}

std::vector<AtNode*> HdArnoldNodeGraph::GetTerminals(const TfToken& terminalName) const
{
    return _nodeGraph.GetTerminals(terminalName);
}

AtNode* HdArnoldNodeGraph::ReadMaterialNetwork(const HdMaterialNetwork& network, const TfToken& terminalType, std::vector<SdfPath>& terminals)
{
    // Nothing to translate here
    if (network.nodes.empty())
        return nullptr;

    // Create a MaterialReader pointing to this HdMaterial. We'll use it to store the list of
    // created nodes in our _nodes list. This way we can properly track the AtNodes that were 
    // generated for this node graph
    MaterialHydraReader materialReader(*this, network, _renderDelegate->GetAPIAdapter());

    // Note that, in Hydra terminology, a relationship input refers to a shader's output attribute
    // and the relationship output refers to the shader input attributes.
    size_t numRelationships = network.relationships.size();
    
    // includedShaders can be used to filter our the list of shaders to translate and 
    // only convert a part of this shading tree. We're currently using this for 
    // displacement with UsdPreviewSurface, where hydra will return us the full 
    // shading network for UsdPreviewSurface but we really just want what is connected
    // to its displacement attribute
    std::unordered_set<SdfPath, TfHash> includedShaders;
    SdfPath terminalPath;
    TfToken terminalId;

    // The network terminal is supposed to be the last node in the list.
    // To ensure about it, we do a reverse loop and see if we recognize one of the terminals
    for (auto it = network.nodes.rbegin(); it != network.nodes.rend(); ++it) {
        auto it2 = std::find(terminals.begin(), terminals.end(), it->path);
        if (it2 != terminals.end()) {
            // We found the terminal
            terminalPath = it->path;
            terminalId = it->identifier;
            // let's remove it from the terminals list, so that next iteration is faster
            terminals.erase(it2);
            break;
        }
    }
    
    // if we didn't recognize the terminal based on the terminals list, 
    // let's just use the latest node in our list
    if (terminalPath.IsEmpty()) {
        terminalPath = network.nodes.back().path;
        terminalId = network.nodes.back().identifier;
    }

    // Special case for UsdPreviewSurface displacement
    if (terminalType == HdMaterialTerminalTokens->displacement && 
            terminalId == str::t_UsdPreviewSurface) {

        const SdfPath& previewId = terminalPath;
        // Check if there is anything connected to it's displacement parameter.
        SdfPath displacementId{};
        for (const auto& relationship : network.relationships) {
            if (relationship.outputId == previewId && relationship.outputName == str::t_displacement &&
                Ai_likely(relationship.inputId != previewId)) {
                displacementId = relationship.inputId;
                break;
            }
        }
        if (displacementId.IsEmpty())
            return nullptr;

        terminalPath = displacementId;
        // Fill the list of included shaders with all the shaders that really
        // need to be translated for displacement
        includedShaders.reserve(network.nodes.size());
        includedShaders.insert(terminalPath);
        bool newNodes = true;
        while(newNodes) {
            newNodes = false;
            for (const auto& relationship : network.relationships) {
                if (includedShaders.find(relationship.outputId) != includedShaders.end() &&
                    includedShaders.find(relationship.inputId) == includedShaders.end()) {      

                    // here's a new node that is connected to another included shader
                    includedShaders.insert(relationship.inputId);
                    newNodes = true;                    
                }
            }
        }
    }

    // ConnectedInputs is a map returning a list of relationships for ech shader path.
    // For each shader to translate, this will tell us which of its input attributes 
    // are connected to another shader
    ConnectedInputs connectedInputs;
    // There can't be more entries in the map, than the amount of nodes or the amount of relationships,
    // let's reserve the map here to avoid reallocation
    connectedInputs.reserve(std::min(network.relationships.size(), network.nodes.size()));
    // We receive a single list of relationships for this network, we want to set them per input shader
    for (size_t i = 0; i < numRelationships; ++i) {
        const HdMaterialRelationship& relationship = network.relationships[i];
        // for hydra, outputId actually refers to the shader which has a connected input attribute
        connectedInputs[relationship.outputId].push_back(&relationship);
    }

    // Loop through all the shaders to translate. For each of them we'll 
    // call ReadShader (from common/materials_utils) with a map of InputAttributes
    InputAttributesList inputAttrs;
    TimeSettings time;
    AtNode* terminalNode = nullptr;
    const SdfPath &id = GetId();
    for (const auto& node : network.nodes) {
        // Check if we only want to translate a filtered list of shaders
        // from this network, and eventually ignore this node
        if (!includedShaders.empty() && 
            includedShaders.find(node.path) == includedShaders.end())
            continue;

        inputAttrs.clear();
        bool isCameraProjection = (node.identifier == str::t_camera_projection);

        // Check if this shader has connected input attributes 
        const auto connectedIt = connectedInputs.find(node.path);
        std::vector<const HdMaterialRelationship*> *connections = nullptr;
        if (connectedIt != connectedInputs.end())
            connections = &connectedIt->second;
        
        // Reserve the input attributes map to the amount of parameter values and eventual connections.
        // This way, there are no reallocations when new elements are added and we avoid costful copies
        inputAttrs.reserve(node.parameters.size() + ((connections) ? connections->size() : size_t(0)));
        // build the input attributes map, where they keys are the attribute names.
        for (const auto& p : node.parameters) {
            // Store this attribute VtValue
            inputAttrs[p.first].value = p.second;
            if (isCameraProjection && p.first == str::t_camera) {
                _renderDelegate->TrackDependencies(GetId(), 
                    HdArnoldRenderDelegate::PathSetWithDirtyBits {
                    {SdfPath(VtValueGetString(p.second)), HdChangeTracker::AllDirty}
                    });
            }
        }
        if (connections) {
            // If there are connections let's have an input attribute for it. 
            // Note that connected attribute won't appear in the above list node.parameters
            for (const auto& c : *connections) {
                inputAttrs[c->outputName].connection = SdfPath(c->inputId.GetString() + ".outputs:" + c->inputName.GetString());
            }
        }
        const SdfPath &nodePath = node.path;
        // If the shader is not already prefixed with its material path, 
        // we add the prefix to the shader name #1940
        std::string arnoldNodeName = nodePath.HasPrefix(id) ?
            nodePath.GetString() : id.GetString() + nodePath.GetString();

        AtNode* arnoldNode = ReadShader(arnoldNodeName, node.identifier, inputAttrs, _renderDelegate->GetAPIAdapter(), time, materialReader);
        // Eventually store the root AtNode if it matches the terminal path
        if (node.path == terminalPath)
            terminalNode = arnoldNode;
    }
    // Return the root shader for this shading network
    return terminalNode;
}

const HdArnoldNodeGraph* HdArnoldNodeGraph::GetNodeGraph(HdRenderIndex* renderIndex, const SdfPath& id)
{
    if (id.IsEmpty()) {
        return nullptr;
    }
    return reinterpret_cast<const HdArnoldNodeGraph*>(renderIndex->GetSprim(HdPrimTypeTokens->material, id));
}

PXR_NAMESPACE_CLOSE_SCOPE
