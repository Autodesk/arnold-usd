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
                if (_nodeGraph.UpdateTerminal(
                        terminal.first,
                        ReadMaterialNetwork(terminal.second, terminal.first == HdMaterialTerminalTokens->displacement, terminals))) {
                    nodeGraphChanged = true;
                }
                // TODO : displacement and previewSurface !

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


AtNode* HdArnoldNodeGraph::ReadMaterialNetwork(const HdMaterialNetwork& network, bool isDisplacement, std::vector<SdfPath>& terminals)
{
    // 
    ConnectedInputs connectedInputs;
    MaterialHydraReader materialReader(*this, network, _renderDelegate->GetAPIAdapter());
    connectedInputs.reserve(std::min(network.relationships.size(), network.nodes.size()));
    // Note that, in Hydra terminology, a relationship input refers to a shader's output attribute
    // and the relationship output refers to the shader input attributes.
    size_t numRelationships = network.relationships.size();
    SdfPath terminalPath;
    if (terminals.size() == 1) {
        terminalPath = terminals[0];
    } else {
        // multiple terminals, we need to analyze which one we need
        
        std::unordered_set<SdfPath, TfHash> terminalPaths;
        terminalPaths.reserve(terminals.size());
        for (const auto& t : terminals)
            terminalPaths.insert(t);

        if (terminalPaths.size() > 1) {
            for (size_t i = 0; i < numRelationships; ++i) {
                auto& terminalIt = terminalPaths.find(network.relationships[i].inputId);
                if (terminalIt != terminalPaths.end()) {
                    // this path has an input connection to it, so it cannot be our terminal node
                    terminalPaths.erase(terminalIt);
                    if (terminalPaths.size() == 1)
                        break;
                }
            }
        }

        if (!terminalPaths.empty()) {
            terminalPath = *terminalPaths.begin();
            auto& it = std::find(terminals.begin(), terminals.end(), terminalPath);
            if (it != terminals.end())
                terminals.erase(it);
            
        }
    }

    // FIXME : What if we didn't find a terminal ????

    for (size_t i = 0; i < numRelationships; ++i) {
        const HdMaterialRelationship& relationship = network.relationships[i];
        connectedInputs[relationship.outputId].push_back(&relationship);
    }

    std::vector<InputAttribute> inputAttrs;
    TimeSettings time;
    AtNode* terminalNode = nullptr;
    for (const auto& node : network.nodes) {
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

/* TODO shader connections
         // Sometimes, the output parameter name effectively acts like a channel connection (ie,
            // UsdUVTexture.outputs:r), so check for this.
            if (relationship.inputName.size() == 1) {
                auto inputType = AiNodeEntryGetOutputType(inputNodeEntry);
                if (relationship.inputName == _tokens->x || relationship.inputName == _tokens->y) {
                    useInputName = (inputType == AI_TYPE_VECTOR || inputType == AI_TYPE_VECTOR2);
                } else if (relationship.inputName == _tokens->z) {
                    useInputName = (inputType == AI_TYPE_VECTOR);
                } else if (
                        relationship.inputName == _tokens->r || relationship.inputName == _tokens->g ||
                        relationship.inputName == _tokens->b) {
                    useInputName = (inputType == AI_TYPE_RGB || inputType == AI_TYPE_RGBA);
                } else if (relationship.inputName == _tokens->a) {
                    useInputName = (inputType == AI_TYPE_RGBA);
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
