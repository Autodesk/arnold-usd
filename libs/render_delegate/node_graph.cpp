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

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
     (x)
     (y)
     (z)
     (r)
     (g)
     (b)
     (a)
     (standard)
     (file)
     (flipT)
     (diffuseTexture)
     (mtlx_surface)
     (texcoord)
     (geomprop)
     (geompropvalue)
     (param_colorspace)
     (ND_standard_surface_surfaceshader)
);
// clang-format on

class MaterialHydraReader : public MaterialReader
{
public:
    MaterialHydraReader(HdArnoldNodeGraph& nodeGraph, 
                    const HdMaterialNetwork& network,
                    HydraArnoldAPI& context) : 
                    _nodeGraph(nodeGraph),
                    _network(network),
                    _context(context),
                    MaterialReader() {}


    AtNode* CreateArnoldNode(const char* nodeType, const char* nodeName) override 
    {        
        return _nodeGraph.CreateArnoldNode(nodeType, nodeName);
    }

    void ConnectShader(AtNode* node, const std::string& attrName, 
            const SdfPath& target) override 
    {
        _context.AddConnection(
            node, attrName.c_str(), target.GetPrimPath().GetText(),
            ArnoldAPIAdapter::CONNECTION_LINK, target.GetElementString());
    }
    
    bool GetShaderInput(const SdfPath& shaderPath, const TfToken& param,
        VtValue& value, TfToken& shaderId) 
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

void HdArnoldNodeGraph::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{    
    const auto id = GetId();
    if ((*dirtyBits & HdMaterial::DirtyResource) && !id.IsEmpty()) {
        HdArnoldRenderParamInterrupt param(renderParam);
        auto value = sceneDelegate->GetMaterialResource(GetId());
        auto nodeGraphChanged = false;
        if (value.IsHolding<HdMaterialNetworkMap>()) {
            param.Interrupt();
            // Mark all nodes as unused before any translation happens.
            const auto& map = value.UncheckedGet<HdMaterialNetworkMap>();
            if (!map.map.empty())
                param.Interrupt();

            std::vector<SdfPath> terminals = map.terminals;
            for (const auto& terminal : map.map) {
                if (terminal.second.nodes.empty())
                    continue;
                AtNode* node = ReadMaterialNetwork(terminal.second, terminal.first, terminals);
                if (node && _nodeGraph.UpdateTerminal(
                        terminal.first, node)) {
                    nodeGraphChanged = true;
                }
                
                if (terminal.first == str::color || terminal.first.GetString().rfind(
                        "light_filter", 0) == 0) {
                    nodeGraphChanged = true;
                    AiUniverseCacheFlush(_renderDelegate->GetUniverse(), AI_CACHE_BACKGROUND);
                }
            }
           // ClearUnusedNodes();
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
    if (network.nodes.empty())
        return nullptr;

    ConnectedInputs connectedInputs;
    MaterialHydraReader materialReader(*this, network, _renderDelegate->GetAPIAdapter());
    connectedInputs.reserve(std::min(network.relationships.size(), network.nodes.size()));
    // Note that, in Hydra terminology, a relationship input refers to a shader's output attribute
    // and the relationship output refers to the shader input attributes.
    size_t numRelationships = network.relationships.size();
    
    std::unordered_set<SdfPath, TfHash> includedShaders;
    SdfPath terminalPath;
    TfToken terminalId;

    // The network terminal is supposed to be the last node in the list.
    // To ensure it, we do a reverse loop and see if we recognize one of the terminals
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

    
    if (terminalPath.IsEmpty())
        terminalPath = network.nodes.back().path;

    for (size_t i = 0; i < numRelationships; ++i) {
        const HdMaterialRelationship& relationship = network.relationships[i];
        connectedInputs[relationship.outputId].push_back(&relationship);
    }

    std::vector<InputAttribute> inputAttrs;
    TimeSettings time;
    AtNode* terminalNode = nullptr;
    for (const auto& node : network.nodes) {
        if (!includedShaders.empty() && 
            includedShaders.find(node.path) == includedShaders.end())
            continue;

        inputAttrs.clear();
        const auto& connectedIt = connectedInputs.find(node.path);
        std::vector<const HdMaterialRelationship*> *connections = nullptr;
        if (connectedIt != connectedInputs.end())
            connections = &connectedIt->second;
        
        inputAttrs.resize(node.parameters.size() + ((connections) ? connections->size() : size_t(0)));
        int pIndex = 0;
        for (const auto& p : node.parameters) {
            inputAttrs[pIndex].name = p.first;
            inputAttrs[pIndex].value = p.second;
            pIndex++;
        }
        if (connections) {
            for (const auto& c : *connections) {
                inputAttrs[pIndex].name = c->outputName;
                inputAttrs[pIndex].connection = SdfPath(c->inputId.GetString() + ".outputs:" + c->inputName.GetString());
                pIndex++;
            }
        }
        AtNode* arnoldNode = ReadShader(node.path.GetString(), node.identifier, inputAttrs, _renderDelegate->GetAPIAdapter(), time, materialReader);
        if (node.path == terminalPath)
            terminalNode = arnoldNode;
    }
    return terminalNode;
}

const HdArnoldNodeGraph* HdArnoldNodeGraph::GetNodeGraph(HdRenderIndex* renderIndex, const SdfPath& id)
{
    if (id.IsEmpty()) {
        return nullptr;
    }
    return reinterpret_cast<const HdArnoldNodeGraph*>(renderIndex->GetSprim(HdPrimTypeTokens->material, id));
}

/* TODO : imagers & co (environment, etc...)

    if (AiNodeEntryGetType(AiNodeGetNodeEntry(inputNode)) == AI_NODE_DRIVER) {
        // imagers are chained with the input parameter
        AiNodeSetPtr(outputNode, str::input, inputNode);
    }
*/

// TODO  preview surface disp
// A single preview surface connected to surface and displacement slots is a common use case, and it needs special
// handling when reading in the network for displacement. We need to check if the output shader is a preview surface
// and see if there is anything connected to its displacement parameter. If the displacement is empty, then we have
// to clear the network.
// The challenge here is that we need to isolate the sub-network connected to the displacement parameter of a
// usd preview surface, and remove any nodes / connections that are not part of it. Since you can mix different
// node types and reuse connections this is not so trivial.


PXR_NAMESPACE_CLOSE_SCOPE
