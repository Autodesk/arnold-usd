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
#include "node_graph.h"

#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#if USD_HAS_MATERIALX

#include <pxr/usd/sdr/registry.h>

#include <pxr/imaging/hdMtlx/hdMtlx.h>

#include <MaterialXCore/Document.h>
#include <MaterialXCore/Node.h>
#include <MaterialXFormat/Util.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXGenOsl/OslShaderGenerator.h>
#include <MaterialXGenShader/Shader.h>
#include <MaterialXGenShader/Util.h>
#include <MaterialXRender/Util.h>

#endif

#include <constant_strings.h>
#include "debug_codes.h"
#include "hdarnold.h"
#include "utils.h"

#include <iostream>
#include <unordered_map>

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
     (ND_standard_surface_surfaceshader)
);
// clang-format on

namespace {

class HydraMaterialNetworkEditContext {
public:
    HydraMaterialNetworkEditContext(HdMaterialNetwork& network, HdMaterialNode& node) : _network(network), _node(node)
    {
    }

    /// Access the value of any parameter on the material.
    ///
    /// This helps the remap function to make decisions about output type or
    /// default values based on existing parameters.
    ///
    /// @param paramName Name of the param.
    /// @return Value of the param wrapped in VtValue.
    VtValue GetParam(const TfToken& paramName)
    {
        const auto paramIt = _node.parameters.find(paramName);
        return paramIt == _node.parameters.end() ? VtValue() : paramIt->second;
    }

    /// Change the value of any parameter on the material.
    ///
    /// This is useful to set default values for parameters before remapping
    /// from existing USD parameters.
    ///
    /// @param paramName Name of the parameter to set.
    /// @param paramValue New value of the parameter wrapped in VtValue.
    void SetParam(const TfToken& paramName, const VtValue& paramValue) { _node.parameters[paramName] = paramValue; }

    /// Change the id of the material.
    ///
    /// This can be used to change the type of the node, ie, change
    /// PxrPreviewSurface to standard_surface as part of the conversion.
    void SetNodeId(const TfToken& nodeId) { _node.identifier = nodeId; }

    /// RenameParam's function is to remap a parameter from the USD/Hydra name
    /// to the arnold name and remap connections.
    ///
    /// @param oldParamName The original, USD/Hydra parameter name.
    /// @param newParamName The new, Arnold parameter name.
    void RenameParam(const TfToken& oldParamName, const TfToken& newParamName)
    {
        const auto oldValue = GetParam(oldParamName);
        if (!oldValue.IsEmpty()) {
            _node.parameters.erase(oldParamName);
            _node.parameters[newParamName] = oldValue;
        }

        for (auto& relationship : _network.relationships) {
            if (relationship.outputId == _node.path && relationship.outputName == oldParamName) {
                relationship.outputName = newParamName;
            }
        }
    }

private:
    HdMaterialNetwork& _network;
    HdMaterialNode& _node;
};

using MaterialEditContext = HydraMaterialNetworkEditContext;

using RemapNodeFunc = void (*)(MaterialEditContext*);

RemapNodeFunc previewSurfaceRemap = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_standard_surface);
    // Defaults that are different from the PreviewSurface. We are setting these
    // before renaming the parameter, so they'll be overwritten with existing values.
    ctx->SetParam(str::t_base_color, VtValue(GfVec3f(0.18f, 0.18f, 0.18f)));
    ctx->SetParam(str::t_base, VtValue(1.0f));
    ctx->SetParam(str::t_emission, VtValue(1.0f));
    ctx->SetParam(str::t_emission_color, VtValue(GfVec3f(0.0f, 0.0f, 0.0f)));
    ctx->SetParam(str::t_specular_color, VtValue(GfVec3f(1.0f, 1.0f, 1.0f)));
    ctx->SetParam(str::t_specular_roughness, VtValue(0.5f));
    ctx->SetParam(str::t_specular_IOR, VtValue(1.5f));
    ctx->SetParam(str::t_coat, VtValue(0.0f));
    ctx->SetParam(str::t_coat_roughness, VtValue(0.01f));

    const auto useSpecularWorkflow = ctx->GetParam(str::t_useSpecularWorkflow);
    // Default value is 0.
    if (useSpecularWorkflow.IsHolding<int>() && useSpecularWorkflow.UncheckedGet<int>() == 1) {
        ctx->RenameParam(str::t_specularColor, str::t_specular_color);
    } else {
        ctx->RenameParam(str::t_metalness, str::t_metallic);
    }

    // Float opacity needs to be remapped to color.
    const auto opacityValue = ctx->GetParam(str::t_opacity);
    if (opacityValue.IsHolding<float>()) {
        const auto opacity = opacityValue.UncheckedGet<float>();
        ctx->SetParam(str::t_opacity, VtValue(GfVec3f(opacity, opacity, opacity)));
    }

    ctx->RenameParam(str::t_diffuseColor, str::t_base_color);
    ctx->RenameParam(str::t_emissiveColor, str::t_emission_color);
    ctx->RenameParam(str::t_roughness, str::t_specular_roughness);
    ctx->RenameParam(str::t_ior, str::t_specular_IOR);
    ctx->RenameParam(str::t_clearcoat, str::t_coat);
    ctx->RenameParam(str::t_clearcoatRoughness, str::t_coat_roughness);
    // We rename the normal to something that doesn't exist for now, because
    // to handle it correctly we would need to make a normal_map node, and
    // hook things up... but this framework doesn't allow for creation of other
    // nodes yet.
    ctx->RenameParam(str::t_normal, str::t_normal_nonexistant_rename);
};

RemapNodeFunc uvTextureRemap = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_image);
    ctx->RenameParam(str::t_file, str::t_filename);
    ctx->RenameParam(str::t_st, str::t_uvcoords);
    ctx->RenameParam(str::t_fallback, str::t_missing_texture_color);
    ctx->RenameParam(str::t_wrapS, str::t_swrap);
    ctx->RenameParam(str::t_wrapT, str::t_twrap);
    for (const auto& param : {str::t_swrap, str::t_twrap}) {
        const auto value = ctx->GetParam(param);
        if (value.IsHolding<TfToken>()) {
            const auto& wrap = value.UncheckedGet<TfToken>();
            if (wrap == str::t_useMetadata) {
                ctx->SetParam(param, VtValue{str::t_file});
            } else if (wrap == str::t_repeat) {
                ctx->SetParam(param, VtValue{str::t_periodic});
            }
        }
    }
    ctx->RenameParam(str::t_scale, str::t_multiply);
    ctx->RenameParam(str::t_bias, str::t_offset);
    // Arnold is using vec3 instead of vec4 for multiply and offset.
    for (const auto& param : {str::t_multiply, str::t_offset}) {
        const auto value = ctx->GetParam(param);
        if (value.IsHolding<GfVec4f>()) {
            const auto& v = value.UncheckedGet<GfVec4f>();
            ctx->SetParam(param, VtValue{GfVec3f{v[0], v[1], v[2]}});
        }
    }
};

RemapNodeFunc floatPrimvarRemap = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_float);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t__default);
};

// Since st and uv is set as the built-in UV parameter on the mesh, we
// have to use a utility node instead of a user_data_rgb node.
RemapNodeFunc float2PrimvarRemap = [](MaterialEditContext* ctx) {
    const auto varnameValue = ctx->GetParam(str::t_varname);
    TfToken varname;
    if (varnameValue.IsHolding<TfToken>()) {
        varname = varnameValue.UncheckedGet<TfToken>();
    } else if (varnameValue.IsHolding<std::string>()) {
        varname = TfToken(varnameValue.UncheckedGet<std::string>());
    }

    // uv and st is remapped to UV coordinates
    if (!varname.IsEmpty() && (varname == str::t_uv || varname == str::t_st)) {
        // We are reading the uv from the mesh.
        ctx->SetNodeId(str::t_utility);
        ctx->SetParam(str::t_color_mode, VtValue(str::t_uv));
        ctx->SetParam(str::t_shade_mode, VtValue(str::t_flat));
    } else {
        ctx->SetNodeId(str::t_user_data_rgb);
        ctx->RenameParam(str::t_varname, str::t_attribute);
    }
    ctx->RenameParam(str::t_fallback, str::t__default);
};

RemapNodeFunc float3PrimvarRemap = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_rgb);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t__default);
};

RemapNodeFunc float4PrimvarRemap = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_rgba);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t__default);
};

RemapNodeFunc intPrimvarRemap = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_int);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t__default);
};

RemapNodeFunc stringPrimvarRemap = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_string);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t__default);
};

RemapNodeFunc transform2dRemap = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_matrix_multiply_vector);
    ctx->RenameParam(str::t_in, str::t_input);
    const auto translateValue = ctx->GetParam(str::t_translation);
    const auto scaleValue = ctx->GetParam(str::t_scale);
    const auto rotateValue = ctx->GetParam(str::t_rotation);

    GfMatrix4f texCoordTransfromMatrix(1.0);
    GfMatrix4f m;
    if (scaleValue.IsHolding<GfVec2f>()) {
        const auto scale = scaleValue.UncheckedGet<GfVec2f>();
        m.SetScale({scale[0], scale[1], 1.0f});
        texCoordTransfromMatrix *= m;
    }
    if (rotateValue.IsHolding<float>()) {
        m.SetRotate(GfRotation(GfVec3d(0.0, 0.0, 1.0), rotateValue.UncheckedGet<float>()));
        texCoordTransfromMatrix *= m;
    }
    if (translateValue.IsHolding<GfVec2f>()) {
        const auto translate = translateValue.UncheckedGet<GfVec2f>();
        m.SetTranslate({translate[0], translate[1], 0.0f});
        texCoordTransfromMatrix *= m;
    }
    ctx->SetParam(str::t_matrix, VtValue(texCoordTransfromMatrix));
};

using NodeRemapFuncs = std::unordered_map<TfToken, RemapNodeFunc, TfToken::HashFunctor>;

const NodeRemapFuncs& _NodeRemapFuncs()
{
    static const NodeRemapFuncs nodeRemapFuncs{
        {str::t_UsdPreviewSurface, previewSurfaceRemap},      {str::t_UsdUVTexture, uvTextureRemap},
        {str::t_UsdPrimvarReader_float, floatPrimvarRemap},   {str::t_UsdPrimvarReader_float2, float2PrimvarRemap},
        {str::t_UsdPrimvarReader_float3, float3PrimvarRemap}, {str::t_UsdPrimvarReader_point, float3PrimvarRemap},
        {str::t_UsdPrimvarReader_normal, float3PrimvarRemap}, {str::t_UsdPrimvarReader_vector, float3PrimvarRemap},
        {str::t_UsdPrimvarReader_float4, float4PrimvarRemap}, {str::t_UsdPrimvarReader_int, intPrimvarRemap},
        {str::t_UsdPrimvarReader_string, stringPrimvarRemap}, {str::t_UsdTransform2d, transform2dRemap},
    };
    return nodeRemapFuncs;
}

// A single preview surface connected to surface and displacement slots is a common use case, and it needs special
// handling when reading in the network for displacement. We need to check if the output shader is a preview surface
// and see if there is anything connected to its displacement parameter. If the displacement is empty, then we have
// to clear the network.
// The challenge here is that we need to isolate the sub-network connected to the displacement parameter of a
// usd preview surface, and remove any nodes / connections that are not part of it. Since you can mix different
// node types and reuse connections this is not so trivial.
void _RemapNetwork(HdMaterialNetwork& network, bool isDisplacement)
{
    // The last node is the output node when using HdMaterialNetworks.
    if (isDisplacement && !network.nodes.empty() && network.nodes.back().identifier == str::t_UsdPreviewSurface) {
        const auto& previewId = network.nodes.back().path;
        // Check if there is anything connected to it's displacement parameter.
        SdfPath displacementId{};
        for (const auto& relationship : network.relationships) {
            if (relationship.outputId == previewId && relationship.outputName == str::t_displacement &&
                Ai_likely(relationship.inputId != previewId)) {
                displacementId = relationship.inputId;
                break;
            }
        }

        auto clearNodes = [&]() {
            network.nodes.clear();
            network.relationships.clear();
        };
        if (displacementId.IsEmpty()) {
            clearNodes();
            return;
        } else {
            // Remove the preview surface.
            network.nodes.pop_back();
            // We need to keep any nodes that are directly or indirectly connected to the displacement node, but we
            // don't have a graph build. We keep an ever growing list of nodes to keep, and keep iterating through
            // the relationships, until there are no more nodes added, with an upper limit of the number of
            // relationships.
            const auto numRelationships = network.relationships.size();
            std::unordered_set<SdfPath, SdfPath::Hash> requiredNodes;
            requiredNodes.insert(displacementId);
            // Upper limit on iterations.
            for (auto i = decltype(numRelationships){0}; i < numRelationships; i += 1) {
                const auto numRequiredNodes = requiredNodes.size();
                for (const auto& relationship : network.relationships) {
                    if (requiredNodes.find(relationship.outputId) != requiredNodes.end()) {
                        requiredNodes.insert(relationship.inputId);
                    }
                }
                // No new required node, break.
                if (numRequiredNodes == requiredNodes.size()) {
                    break;
                }
            }

            // Clear out the relationships we don't need.
            for (size_t i = 0; i < network.relationships.size(); i += 1) {
                if (requiredNodes.find(network.relationships[i].outputId) == requiredNodes.end()) {
                    network.relationships.erase(network.relationships.begin() + i);
                    i -= 1;
                }
            }
            // Clear out the nodes we don't need.
            for (size_t i = 0; i < network.nodes.size(); i += 1) {
                if (requiredNodes.find(network.nodes[i].path) == requiredNodes.end()) {
                    network.nodes.erase(network.nodes.begin() + i);
                    i -= 1;
                }
            }
        }
    }
    auto isUVTexture = [&](const SdfPath& id) -> bool {
        for (const auto& material : network.nodes) {
            if (material.path == id && material.identifier == str::t_UsdUVTexture) {
                return true;
            }
        }
        return false;
    };

    auto isSTFloat2PrimvarReader = [&](const SdfPath& id) -> bool {
        for (const auto& material : network.nodes) {
            if (material.path == id && material.identifier == str::t_UsdPrimvarReader_float2) {
                const auto paramIt = material.parameters.find(str::t_varname);
                TfToken token;

                if (paramIt != material.parameters.end()) {
                    if (paramIt->second.IsHolding<TfToken>()) {
                        token = paramIt->second.UncheckedGet<TfToken>();
                    } else if (paramIt->second.IsHolding<std::string>()) {
                        token = TfToken(paramIt->second.UncheckedGet<std::string>());
                    }
                }

                return !token.IsEmpty() && (token == str::t_uv || token == str::t_st);
            }
        }
        return false;
    };
    // We are invalidating any float 2 primvar reader connection with either uv
    // or st primvar to a usd uv texture.
    for (auto& relationship : network.relationships) {
        if (relationship.outputName == str::t_st) {
            // We check if the node is a UsdUVTexture
            if (isUVTexture(relationship.outputId) && isSTFloat2PrimvarReader(relationship.inputId)) {
                // We need to keep the inputId, otherwise we won't be able to find
                // the entry point to the shader network.
                relationship.outputId = SdfPath();
            }
        }
    }

    for (auto& material : network.nodes) {
        const auto remapIt = _NodeRemapFuncs().find(material.identifier);
        if (remapIt == _NodeRemapFuncs().end()) {
            continue;
        }

        HydraMaterialNetworkEditContext editCtx(network, material);
        remapIt->second(&editCtx);
    }
}

} // namespace

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
            SetNodesUnused();
            const auto& map = value.UncheckedGet<HdMaterialNetworkMap>();
            auto readNetwork = [&](const HdMaterialNetwork* network, bool isDisplacement) -> AtNode* {
                if (network == nullptr) {
                    return nullptr;
                }
                // No need to interrupt earlier as we don't know if there is a valid network passed to the function or
                // not.
                param.Interrupt();
                // We are remapping the preview surface nodes to ones that are supported
                // in Arnold. This way we can keep the export code untouched,
                // and handle connection / node exports separately.
                auto remappedNetwork = *network;
                _RemapNetwork(remappedNetwork, isDisplacement);
                return ReadMaterialNetwork(remappedNetwork);
            };
            for (const auto& terminal : map.map) {
                if (_nodeGraph.UpdateTerminal(
                        terminal.first,
                        readNetwork(&terminal.second, terminal.first == HdMaterialTerminalTokens->displacement))) {
                    nodeGraphChanged = true;
                }
                if (terminal.first == str::color || terminal.first.GetString().rfind(
                        "light_filter", 0) == 0) {
                    nodeGraphChanged = true;
                    AiUniverseCacheFlush(_renderDelegate->GetUniverse(), AI_CACHE_BACKGROUND);
                }
            }
            ClearUnusedNodes();
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

#ifdef USD_HAS_MATERIAL_NETWORK2
bool HdArnoldNodeGraph::ReadMaterialNetwork(const HdMaterialNetwork2& network)
{
    auto readTerminal = [&](const TfToken& name) -> AtNode* {
        const auto* terminal = TfMapLookupPtr(network.terminals, name);
        if (terminal == nullptr) {
            return nullptr;
        }
        return ReadMaterialNode(network, terminal->upstreamNode);
    };
    auto terminalChanged = false;
    for (const auto& terminal : network.terminals) {
        if (terminal.first == dMaterialTerminalTokens->surface) {
            terminalChanged |= _nodeGraph.UpdateTerminal(terminal.first, readMaterialXTerminal(terminal.first));
        } else {
            terminalChanged |= _nodeGraph.UpdateTerminal(terminal.first, readTerminal(terminal.first));
        }
    }
    return terminalChanged;
};

AtNode* HdArnoldNodeGraph::ReadMaterialNode(const HdMaterialNetwork2& network, const SdfPath& nodePath)
{
    const auto* node = TfMapLookupPtr(network.nodes, nodePath);
    // We don't expect this to happen.
    if (Ai_unlikely(node == nullptr)) {
        return nullptr;
    }
    // TODO(pal): This logic should be moved to GetNode, and we can cache the nodeType on the NodeData.
    const auto* nodeTypeStr = node->nodeTypeId.GetText();
    bool isMaterialx = false;
    if (node->nodeTypeId.size() > 3 && nodeTypeStr[0] == 'N' && nodeTypeStr[1] == 'D' && nodeTypeStr[2] == '_') {
        isMaterialx = true;
    }
    const AtString nodeType(strncmp(nodeTypeStr, "arnold:", 7) == 0 ? nodeTypeStr + 7 : nodeTypeStr);
    TF_DEBUG(HDARNOLD_MATERIAL)
        .Msg("HdArnoldNodeGraph::ReadMaterial - node %s - type %s\n", nodePath.GetText(), nodeType.c_str());
    auto localNode = GetNode(nodePath, nodeType);
    if (localNode == nullptr || localNode->node == nullptr) {
        return nullptr;
    }
    auto* ret = localNode->node;
    if (localNode->used) {
        return ret;
    }
    localNode->used = true;
    // If we are translating an inline OSL node, the code parameter needs to be set first, then the rest of the
    // parameters so we can ensure the parameters are set.
    const auto isOSL = AiNodeIs(ret, str::osl);
    if (isOSL && !isMaterialx) {
        const auto param = node->parameters.find(str::t_code);
        if (param != node->parameters.end()) {
            HdArnoldSetParameter(ret, AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(ret), str::code), param->second);
        }
    }
    // We need to query the node entry AFTER setting the code parameter on the node.
    const auto* nentry = AiNodeGetNodeEntry(ret);
    for (const auto& param : node->parameters) {
        const auto& paramName = param.first;
        // Code is already set.
        if (isOSL && paramName == str::t_code) {
            continue;
        }
        std::string paramNameStr = paramName.GetText();
        if (isMaterialx)
            paramNameStr = std::string("param_shader_") + paramNameStr;

        const auto* pentry = AiNodeEntryLookUpParameter(nentry, AtString(paramNameStr.c_str()));
        if (pentry == nullptr) {
            continue;
        }
        HdArnoldSetParameter(ret, pentry, param.second);
    }
    // Translate connections. We expect that the stack will be big enough to handle recursion to the network.
    for (const auto& inputConnection : node->inputConnections) {
        // We can have multiple connections for AtArray parameters.
        const auto& connections = inputConnection.second;
        if (connections.empty() || connections.size() > 1) {
            continue;
        }
        // TODO(pal): Support connections to array parameters.
        const auto& connection = connections[0];
        auto* upstreamNode = ReadMaterialNode(network, connection.upstreamNode);
        if (upstreamNode == nullptr) {
            continue;
        }
        // Check if the parameter exists.
        if (AiNodeEntryLookUpParameter(nentry, AtString(inputConnection.first.GetText())) == nullptr) {
            continue;
        }

        // Arnold nodes can only have one output, but you can connect to sub-components.
        // USD doesn't yet have component connections / swizzling, but it's nodes can have multiple
        // outputs to which you can connect.
        // Sometimes, the output parameter name effectively acts like a channel inputConnection (ie,
        // UsdUVTexture.outputs:r), so check for this.
        bool useUpstreamName = false;
        if (connection.upstreamOutputName.size() == 1) {
            const auto* upstreamNodeEntry = AiNodeGetNodeEntry(upstreamNode);
            auto upstreamType = AiNodeEntryGetOutputType(upstreamNodeEntry);
            if (connection.upstreamOutputName == _tokens->x || connection.upstreamOutputName == _tokens->y) {
                useUpstreamName = (upstreamType == AI_TYPE_VECTOR || upstreamType == AI_TYPE_VECTOR2);
            } else if (connection.upstreamOutputName == _tokens->z) {
                useUpstreamName = (upstreamType == AI_TYPE_VECTOR);
            } else if (
                connection.upstreamOutputName == _tokens->r || connection.upstreamOutputName == _tokens->g ||
                connection.upstreamOutputName == _tokens->b) {
                useUpstreamName = (upstreamType == AI_TYPE_RGB || upstreamType == AI_TYPE_RGBA);
            } else if (connection.upstreamOutputName == _tokens->a) {
                useUpstreamName = (upstreamType == AI_TYPE_RGBA);
            }
        }
        if (useUpstreamName) {
            AiNodeLinkOutput(
                upstreamNode, connection.upstreamOutputName.GetText(), ret, inputConnection.first.GetText());
        } else {
            AiNodeLink(upstreamNode, inputConnection.first.GetText(), ret);
        }
    }
    return ret;
}

#else

AtNode* HdArnoldNodeGraph::ReadMaterialNetwork(const HdMaterialNetwork& network)
{
    std::vector<AtNode*> nodes;
    nodes.reserve(network.nodes.size());
    for (const auto& node : network.nodes) {
        auto* n = ReadMaterialNode(node);
        if (n != nullptr) {
            nodes.push_back(n);
        }
    }
    
    // We have to return the entry point from this function, and there are
    // no hard guarantees that the last node (or the first) is going to be the
    // entry point to the network, so we look for the first node that's not the
    // source to any of the connections.
    for (const auto& relationship : network.relationships) {
        auto* inputNode = FindNode(relationship.inputId);
        if (inputNode == nullptr) {
            continue;
        }
        nodes.erase(std::remove(nodes.begin(), nodes.end(), inputNode), nodes.end());
        auto* outputNode = FindNode(relationship.outputId);
        if (outputNode == nullptr) {
            continue;
        }
        const auto* outputNodeEntry = AiNodeGetNodeEntry(outputNode);
        std::string outputAttr = relationship.outputName.GetText();
        if (AiNodeEntryLookUpParameter(outputNodeEntry, AtString(outputAttr.c_str())) == nullptr) {
            // Attribute outputAttr wasn't found in outputNode. First we need to check if it's an array connection
            std::string baseOutputAttr;
            size_t elemPos = outputAttr.rfind(":i");
            if (elemPos != std::string::npos && elemPos > 0) {
                // We have an array connection, e.g. "color:i0".
                // We want to replace this string by "color[0]" which Arnold understands
                baseOutputAttr = outputAttr.substr(0, elemPos);
                outputAttr.replace(elemPos, 2, std::string("["));
                outputAttr += "]";
            }
            // if we didn't recognize an array connection, or if the 
            // corresponding attribute doesn't exist in the arnold node entry, 
            // we want to skip this connection
            if (baseOutputAttr.empty() || 
                (AiNodeEntryLookUpParameter(outputNodeEntry, AtString(baseOutputAttr.c_str())) == nullptr))
                continue;
        }

        // Arnold nodes can only have one output... but you can connect to sub components of them.
        // USD doesn't yet have component connections / swizzling, but it's nodes can have multiple
        // outputs to which you can connect.
        // Sometimes, the output parameter name effectively acts like a channel connection (ie,
        // UsdUVTexture.outputs:r), so check for this.
        bool useInputName = false;
        if (relationship.inputName.size() == 1) {
            const auto* inputNodeEntry = AiNodeGetNodeEntry(inputNode);
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
        }
        if (useInputName) {
            AiNodeLinkOutput(
                inputNode, relationship.inputName.GetText(), outputNode, outputAttr.c_str());
        } else {
            AiNodeLink(inputNode, outputAttr.c_str(), outputNode);
        }
    }

    auto* entryPoint = nodes.empty() ? nullptr : nodes.front();
    return entryPoint;
}

AtNode* HdArnoldNodeGraph::ReadMaterialNode(const HdMaterialNode& node)
{
    const auto* nodeTypeStr = node.identifier.GetText();
    bool isMaterialx = false;
    const AtString nodeType(strncmp(nodeTypeStr, "arnold:", 7) == 0 ? nodeTypeStr + 7 : nodeTypeStr);
    if (node.identifier != str::t_standard_surface && strncmp(nodeTypeStr, "ND_", 3) == 0) {
        isMaterialx = true;
    }

    TF_DEBUG(HDARNOLD_MATERIAL)
        .Msg("HdArnoldNodeGraph::ReadMaterial - node %s - type %s\n", node.path.GetText(), nodeType.c_str());
    auto localNode = GetNode(node.path, nodeType);
    if (localNode == nullptr || localNode->node == nullptr) {
        return nullptr;
    }
    auto* ret = localNode->node;
    if (localNode->used) {
        return ret;
    }
    localNode->used = true;
    // If we are translating an inline OSL node, the code parameter needs to be set first, then the rest of the
    // parameters so we can ensure the parameters are set.
    const auto isOSL = AiNodeIs(ret, str::osl);
    if (isOSL && !isMaterialx) {
        const auto param = node.parameters.find(str::t_code);
        if (param != node.parameters.end()) {
            HdArnoldSetParameter(ret, AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(ret), str::code), param->second);
        }
    }
    // We need to query the node entry AFTER setting the code parameter on the node.
    const auto* nentry = AiNodeGetNodeEntry(ret);
    for (const auto& param : node.parameters) {
        const auto& paramName = param.first;
        // Code is already set.
        if (isOSL && paramName == str::t_code) {
            continue;
        }
        std::string paramNameStr = paramName.GetText();
        if (isMaterialx)
            paramNameStr = std::string("param_shader_") + paramNameStr;
        const auto* pentry = AiNodeEntryLookUpParameter(nentry, AtString(paramNameStr.c_str()));
        if (pentry == nullptr) {
            continue;
        }
        if (isMaterialx && paramNameStr == "param_shader_file") {
            AtString fileStr;
            const static AtString textureSourceStr("textureresource");
            if (AiMetaDataGetStr(nentry, str::param_shader_file, str::osl_struct, &fileStr) && 
                fileStr == textureSourceStr)
            {
                const static AtString tx_code("struct textureresource { string filename; string colorspace; };\n"
                    "shader texturesource_input(string filename = \"\", string colorspace = \"\", "
                    "output textureresource out = {filename, colorspace}){}");
                std::string resourceNodeName = std::string(AiNodeGetName(ret)) + std::string("_texturesource");
                // Create an additional osl shader, for the texture resource. Set it the
                // hardcoded osl code above
                const SdfPath resourceNodePath(resourceNodeName);
                const auto resourceNodeIt = _nodes.find(resourceNodePath);

                AtNode *oslSource = nullptr;
                if (resourceNodeIt != _nodes.end())
                    oslSource = resourceNodeIt->second->node;

                if (oslSource == nullptr) {
                    oslSource = AiNode(_renderDelegate->GetUniverse(), str::osl, resourceNodeName.c_str());
                    AiNodeSetStr(oslSource, str::code, tx_code);
                    auto resourceNodeData = NodeDataPtr(new NodeData(oslSource, true));
                    _nodes.emplace(resourceNodePath, resourceNodeData); 
                }                

                // Set the actual texture filename to this new osl shader
                const auto* pChildEntry = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(oslSource), AtString("param_filename"));
                HdArnoldSetParameter(oslSource, pChildEntry, param.second);
                                
                // Connect the original osl shader attribute to our new osl shader
                AiNodeLink(oslSource,str::param_shader_file, ret);
                continue;    
            }
        }

        HdArnoldSetParameter(ret, pentry, param.second);
    }
    
    return ret;
}
#endif

AtNode* HdArnoldNodeGraph::FindNode(const SdfPath& id) const
{
    const auto nodeIt = _nodes.find(id);
    return nodeIt == _nodes.end() ? nullptr : nodeIt->second->node;
}

AtString HdArnoldNodeGraph::GetLocalNodeName(const SdfPath& path) const
{
    const auto* pp = path.GetText();
    if (pp == nullptr || pp[0] == '\0') {
        return AtString(path.GetText());
    }
    const auto p = GetId().AppendPath(SdfPath(TfToken(pp + 1)));
    return AtString(p.GetText());
}

HdArnoldNodeGraph::NodeDataPtr HdArnoldNodeGraph::GetNode(const SdfPath& path, const AtString& nodeType)
{
    const auto nodeIt = _nodes.find(path);
    // If the node already exists, we are checking if the node type is the same
    // as the requested node type. While this is not meaningful for applications
    // like usdview, which rebuild their scene every in case of changes like this,
    // this is still useful for more interactive applications which keep the
    // render index around for longer times, like Maya to Hydra.
    if (nodeIt != _nodes.end()) {
        if (AiNodeEntryGetNameAtString(AiNodeGetNodeEntry(nodeIt->second->node)) != nodeType) {
            TF_DEBUG(HDARNOLD_MATERIAL).Msg("  existing node found, but type mismatch - deleting old node\n");
            _nodes.erase(nodeIt);
        } else {
            TF_DEBUG(HDARNOLD_MATERIAL).Msg("  existing node found - using it\n");
            // This is the first time an existing node is queried, we need to reset the node. We do the reset here
            // to avoid blindly resetting all the nodes when calling SetNodesUnused.
            if (!nodeIt->second->used && nodeIt->second->node != nullptr) {
                AiNodeReset(nodeIt->second->node);
            }
            return nodeIt->second;
        }
    }
    const AtString nodeName = GetLocalNodeName(path);
    // first check if there is a materialx shader associated to this node type
    AtNode* node = GetMaterialxShader(nodeType, nodeName); 

    if (node == nullptr)
        node = AiNode(_renderDelegate->GetUniverse(), nodeType, nodeName);
    auto ret = NodeDataPtr(new NodeData(node, false));
    _nodes.emplace(path, ret);
    if (ret == nullptr) {
        TF_DEBUG(HDARNOLD_MATERIAL).Msg("  unable to create node of type %s - aborting\n", nodeType.c_str());
        return nullptr;
    }
    
    return ret;
}

AtNode *HdArnoldNodeGraph::GetMaterialxShader(const AtString &nodeType, const AtString &nodeName)
{
#if ARNOLD_VERSION_NUMBER < 70103
    return nullptr;
#endif
    const char *nodeTypeChar = nodeType.c_str();
    if (nodeType == str::ND_standard_surface_surfaceshader) {
        AtNode *node = AiNode(_renderDelegate->GetUniverse(), str::standard_surface, nodeName);
        return node;
    } else if (nodeType.length() > 3 && nodeTypeChar[0] == 'N' && nodeTypeChar[1] == 'D' && nodeTypeChar[2] == '_') {
        // Create an OSL inline shader
        AtNode *node = AiNode(_renderDelegate->GetUniverse(), str::osl, nodeName);
        // Get the OSL description of this mtlx shader. Its attributes will be prefixed with 
        // "param_shader_"
        AtString oslCode = AiMaterialxGetOslShaderCode(nodeType.c_str(), "shader");
        // Set the OSL code. This will create a new AtNodeEntry with parameters
        // based on the osl code
        AiNodeSetStr(node, str::code, oslCode);
        return node;
    }
    return nullptr;
}
bool HdArnoldNodeGraph::ClearUnusedNodes()
{
    // We are removing any shaders that has not been used during material
    // translation.
    // We only have guarantees to erase elements during iteration since C++14.
    std::vector<SdfPath> nodesToRemove;
    for (auto& it : _nodes) {
        if (!it.second->used) {
            if (it.second->node != nullptr) {
                if (_nodeGraph.ContainsTerminal(it.second->node)) {
                    TF_CODING_ERROR(
                        "[HdArnold] Entry point to the material network is not translated! %s",
                        AiNodeGetName(it.second->node));
                    return false;
                }
            }
            nodesToRemove.push_back(it.first);
        }
    }
    for (const auto& it : nodesToRemove) {
        _nodes.erase(it);
    }
    return true;
}

void HdArnoldNodeGraph::SetNodesUnused()
{
    for (auto& it : _nodes) {
        it.second->used = false;
    }
}

const HdArnoldNodeGraph* HdArnoldNodeGraph::GetNodeGraph(HdRenderIndex* renderIndex, const SdfPath& id)
{
    if (id.IsEmpty()) {
        return nullptr;
    }
    return reinterpret_cast<const HdArnoldNodeGraph*>(renderIndex->GetSprim(HdPrimTypeTokens->material, id));
}



PXR_NAMESPACE_CLOSE_SCOPE
