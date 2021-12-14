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
);
// clang-format on

namespace {

#ifdef USD_HAS_MATERIAL_NETWORK2

class HydraMaterialNetwork2EditContext {
public:
    HydraMaterialNetwork2EditContext(HdMaterialNode2& node) : _node(node) {}

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
    void SetNodeId(const TfToken& nodeId) { _node.nodeTypeId = nodeId; }

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

        // We can't rename output parameters, so this is simplified.
        auto oldConnections = TfMapLookupPtr(_node.inputConnections, oldParamName);
        if (oldConnections != nullptr) {
            _node.inputConnections[newParamName] = *oldConnections;
            _node.inputConnections.erase(oldParamName);
        }
    }

private:
    HdMaterialNode2& _node;
};

using MaterialEditContext = HydraMaterialNetwork2EditContext;

#else
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
#endif

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

#ifdef USD_HAS_MATERIAL_NETWORK2
void _RemapNetwork(HdMaterialNetwork2& network) {}
#else
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
#endif

} // namespace

HdArnoldMaterial::HdArnoldMaterial(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdMaterial(id), _renderDelegate(renderDelegate)
{
    _material.surface = _renderDelegate->GetFallbackSurfaceShader();
    _material.volume = _renderDelegate->GetFallbackVolumeShader();
}

HdArnoldMaterial::~HdArnoldMaterial() { _renderDelegate->RemoveMaterial(GetId()); }

void HdArnoldMaterial::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    const auto id = GetId();
    if ((*dirtyBits & HdMaterial::DirtyResource) && !id.IsEmpty()) {
        HdArnoldRenderParamInterrupt param(renderParam);
        auto value = sceneDelegate->GetMaterialResource(GetId());
        ArnoldMaterial material;
        if (value.IsHolding<HdMaterialNetworkMap>()) {
            param.Interrupt();
            // Mark all nodes as unused before any translation happens.
            SetNodesUnused();
            const auto& map = value.UncheckedGet<HdMaterialNetworkMap>();
#ifdef USD_HAS_MATERIAL_NETWORK2
            HdMaterialNetwork2 network;
            HdMaterialNetwork2ConvertFromHdMaterialNetworkMap(map, &network, nullptr);
            // Apply UsdPreviewSurface -> Arnold shaders remapping logic.
            _RemapNetwork(network);
            // Convert the material network to Arnold shaders.
            ReadMaterialNetwork(network, material);
#else
            const auto* surfaceNetwork = TfMapLookupPtr(map.map, HdMaterialTerminalTokens->surface);
            const auto* displacementNetwork = TfMapLookupPtr(map.map, HdMaterialTerminalTokens->displacement);
            const auto* volumeNetwork = TfMapLookupPtr(map.map, HdMaterialTerminalTokens->volume);
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
            material.surface = readNetwork(surfaceNetwork, false);
            material.displacement = readNetwork(displacementNetwork, true);
            material.volume = readNetwork(volumeNetwork, false);
#endif
            ClearUnusedNodes(material);
        }
        const auto materialChanged = _material.UpdateMaterial(material, _renderDelegate);
        // We only mark the material dirty if one of the terminals have changed, but ignore the initial sync, because we
        // expect Hydra to do the initial assignment correctly.
        if (_wasSyncedOnce && materialChanged) {
            _renderDelegate->DirtyMaterial(id);
        }
    }
    *dirtyBits = HdMaterial::Clean;
    _wasSyncedOnce = true;
}

HdDirtyBits HdArnoldMaterial::GetInitialDirtyBitsMask() const { return HdMaterial::DirtyResource; }

AtNode* HdArnoldMaterial::GetSurfaceShader() const { return _material.surface; }

AtNode* HdArnoldMaterial::GetDisplacementShader() const { return _material.displacement; }

AtNode* HdArnoldMaterial::GetVolumeShader() const { return _material.volume; }

#ifdef USD_HAS_MATERIAL_NETWORK2
void HdArnoldMaterial::ReadMaterialNetwork(const HdMaterialNetwork2& network, ArnoldMaterial& material)
{
    auto readTerminal = [&](const TfToken& name) -> AtNode* {
        const auto* terminal = TfMapLookupPtr(network.terminals, name);
        if (terminal == nullptr) {
            return nullptr;
        }
        return ReadMaterialNode(network, terminal->upstreamNode);
    };
#if USD_HAS_MATERIALX
    auto readMaterialXTerminal = [&](const TfToken& name) -> AtNode* {
        // First we check if the surface terminal node is from materialx.
        const auto* terminalConnection = TfMapLookupPtr(network.terminals, name);
        if (terminalConnection == nullptr) {
            return nullptr;
        }
        const auto* terminal = TfMapLookupPtr(network.nodes, terminalConnection->upstreamNode);
        if (terminal == nullptr) {
            return nullptr;
        }
        auto& sdrRegistry = SdrRegistry::GetInstance();
        // Check if the terminal node is a mtlx node type.
        if (sdrRegistry.GetShaderNodeByIdentifierAndType(terminal->nodeTypeId, str::t_mtlx)) {
            for (auto* node : _materialxNodes) {
                AiNodeDestroy(node);
            }
            MaterialX::FileSearchPath searchPath;
            // TODO(pal): grab the paths from the Arnold SDK.
            MaterialX::FilePathVec libraryFolders = {"materialx"};
            searchPath.append(MaterialX::FilePath(ARNOLD_MATERIALX_BASE_DIR));
            searchPath.append(MaterialX::FilePath(ARNOLD_MATERIALX_STDLIB_DIR));
            MaterialX::DocumentPtr stdLibraries = MaterialX::createDocument();
            MaterialX::loadLibraries(libraryFolders, searchPath, stdLibraries);
            std::set<SdfPath> textureNodes;
            MaterialX::StringMap mtlxTextureMap;
            auto mtlxDoc = HdMtlxCreateMtlxDocumentFromHdNetwork(
                network, *terminal, GetId(), stdLibraries, &textureNodes, &mtlxTextureMap);
            for (const auto& texturePath : textureNodes) {
                const auto* textureNode = TfMapLookupPtr(network.nodes, texturePath);
                if (textureNode == nullptr) {
                    continue;
                }
                const auto* fileValue = TfMapLookupPtr(textureNode->parameters, str::t_file);
                if (fileValue == nullptr) {
                    continue;
                }
                const auto nodeGraph = mtlxDoc->getNodeGraph(texturePath.GetParentPath().GetName());
                const auto texture = nodeGraph->getNode(texturePath.GetName());
                if (fileValue->IsHolding<SdfAssetPath>()) {
                    const auto resolvedPath = fileValue->UncheckedGet<SdfAssetPath>().GetResolvedPath();
                    texture->setInputValue(str::t_file.GetText(), resolvedPath, str::t_filename.GetText());
                }
                // As a workaround for now, we are checking any connections to the texcoord parameter of the image
                // node, and removing any geompropvalue if it points to st or uv, otherwise accessing the texture
                // won't work as of now.
                auto input = texture->getInput(_tokens->texcoord.GetString());
                if (input == nullptr) {
                    continue;
                }
                auto outputNode = input->getConnectedNode();
                if (outputNode == nullptr) {
                    continue;
                }
                // We are only interested in geompropvalues.
                if (outputNode->getCategory() != _tokens->geompropvalue.GetString()) {
                    continue;
                }

                auto geomprop = outputNode->getInput(_tokens->geomprop.GetString());
                if (geomprop == nullptr) {
                    continue;
                }

                // It's a geompropvalue pointing to st or uv, we remove the input from the texture node, but keep the
                // geomprop intact.
                if (geomprop->getValueString() == str::t_st.GetString() ||
                    geomprop->getValueString() == str::t_uv.GetString()) {
                    texture->removeInput(_tokens->texcoord.GetString());
                }
            }
            std::stringstream ss;
            MaterialX::writeToXmlStream(mtlxDoc, ss);
            auto* nodes = AiArrayAllocate(0, 1, AI_TYPE_NODE);
            auto* params = AiParamValueMap();
            AiParamValueMapSetStr(
                params, str::shader_prefix, AtString{(GetId().GetString() + "/mtlx_" + name.GetString()).c_str()});
            AiMaterialxReadMaterials(_renderDelegate->GetUniverse(), ss.str().c_str(), params, nodes);
            const auto numNodes = AiArrayGetNumElements(nodes);
            AtNode* ret = nullptr;
            _materialxNodes.reserve(numNodes);
            for (auto i = decltype(numNodes){0}; i < numNodes; i += 1) {
                auto* node = static_cast<AtNode*>(AiArrayGetPtr(nodes, i));
                if (ret == nullptr &&
                    AiNodeLookUpUserParameter(node, ("material_" + name.GetString()).c_str()) != nullptr) {
                    ret = node;
                }
                _materialxNodes.push_back(ret);
            }
            return ret;
        } else {
            return ReadMaterialNode(network, terminalConnection->upstreamNode);
        }
    };

    material.surface = readMaterialXTerminal(HdMaterialTerminalTokens->surface);
#else
    material.surface = readTerminal(HdMaterialTerminalTokens->surface);
#endif
    material.displacement = readTerminal(HdMaterialTerminalTokens->displacement);
    material.volume = readTerminal(HdMaterialTerminalTokens->volume);
};

AtNode* HdArnoldMaterial::ReadMaterialNode(const HdMaterialNetwork2& network, const SdfPath& nodePath)
{
    const auto* node = TfMapLookupPtr(network.nodes, nodePath);
    // We don't expect this to happen.
    if (Ai_unlikely(node == nullptr)) {
        return nullptr;
    }
    // TODO(pal): This logic should be moved to GetNode, and we can cache the nodeType on the NodeData.
    const auto* nodeTypeStr = node->nodeTypeId.GetText();
    const AtString nodeType(strncmp(nodeTypeStr, "arnold:", 7) == 0 ? nodeTypeStr + 7 : nodeTypeStr);
    TF_DEBUG(HDARNOLD_MATERIAL)
        .Msg("HdArnoldMaterial::ReadMaterial - node %s - type %s\n", nodePath.GetText(), nodeType.c_str());
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
    if (isOSL) {
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
        const auto* pentry = AiNodeEntryLookUpParameter(nentry, AtString(paramName.GetText()));
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

AtNode* HdArnoldMaterial::ReadMaterialNetwork(const HdMaterialNetwork& network)
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
        if (AiNodeEntryLookUpParameter(outputNodeEntry, AtString(relationship.outputName.GetText())) == nullptr) {
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
                inputNode, relationship.inputName.GetText(), outputNode, relationship.outputName.GetText());
        } else {
            AiNodeLink(inputNode, relationship.outputName.GetText(), outputNode);
        }
    }

    auto* entryPoint = nodes.empty() ? nullptr : nodes.front();
    return entryPoint;
}

AtNode* HdArnoldMaterial::ReadMaterialNode(const HdMaterialNode& node)
{
    const auto* nodeTypeStr = node.identifier.GetText();
    const AtString nodeType(strncmp(nodeTypeStr, "arnold:", 7) == 0 ? nodeTypeStr + 7 : nodeTypeStr);
    TF_DEBUG(HDARNOLD_MATERIAL)
        .Msg("HdArnoldMaterial::ReadMaterial - node %s - type %s\n", node.path.GetText(), nodeType.c_str());
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
    if (isOSL) {
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
        const auto* pentry = AiNodeEntryLookUpParameter(nentry, AtString(paramName.GetText()));
        if (pentry == nullptr) {
            continue;
        }
        HdArnoldSetParameter(ret, pentry, param.second);
    }
    return ret;
}
#endif

AtNode* HdArnoldMaterial::FindNode(const SdfPath& id) const
{
    const auto nodeIt = _nodes.find(id);
    return nodeIt == _nodes.end() ? nullptr : nodeIt->second->node;
}

AtString HdArnoldMaterial::GetLocalNodeName(const SdfPath& path) const
{
    const auto* pp = path.GetText();
    if (pp == nullptr || pp[0] == '\0') {
        return AtString(path.GetText());
    }
    const auto p = GetId().AppendPath(SdfPath(TfToken(pp + 1)));
    return AtString(p.GetText());
}

HdArnoldMaterial::NodeDataPtr HdArnoldMaterial::GetNode(const SdfPath& path, const AtString& nodeType)
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
    auto* node = AiNode(_renderDelegate->GetUniverse(), nodeType);
    auto ret = NodeDataPtr(new NodeData(node, false));
    _nodes.emplace(path, ret);
    if (ret == nullptr) {
        TF_DEBUG(HDARNOLD_MATERIAL).Msg("  unable to create node of type %s - aborting\n", nodeType.c_str());
        return nullptr;
    }
    const auto nodeName = GetLocalNodeName(path);
    AiNodeSetStr(node, str::name, nodeName);
    return ret;
}

bool HdArnoldMaterial::ClearUnusedNodes(const ArnoldMaterial& material)
{
    // We are removing any shaders that has not been used during material
    // translation.
    // We only have guarantees to erase elements during iteration since C++14.
    std::vector<SdfPath> nodesToRemove;
    for (auto& it : _nodes) {
        if (!it.second->used) {
            if (it.second->node != nullptr) {
                if (it.second->node == material.surface || it.second->node == material.displacement ||
                    it.second->node == material.volume) {
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

void HdArnoldMaterial::SetNodesUnused()
{
    for (auto& it : _nodes) {
        it.second->used = false;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
