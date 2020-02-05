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
#include "material.h"

#include <pxr/usdImaging/usdImaging/tokens.h>

#include "constant_strings.h"
#include "debug_codes.h"
#include "hdarnold.h"
#include "utils.h"

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
     (katanaColor)
     (flipT)
     (diffuseTexture)
);
// clang-format on

namespace {

class MaterialEditContext {
public:
    MaterialEditContext() = default;

    virtual ~MaterialEditContext() = default;

    MaterialEditContext(const MaterialEditContext&) = delete;

    MaterialEditContext(MaterialEditContext&&) = delete;

    MaterialEditContext& operator=(const MaterialEditContext&) = delete;

    MaterialEditContext& operator=(MaterialEditContext&&) = delete;

    /// Access the value of any parameter on the material.
    ///
    /// This helps the remap function to make decisions about output type or
    /// default values based on existing parameters.
    ///
    /// @param paramName Name of the param.
    /// @return Value of the param wrapped in VtValue.
    virtual VtValue GetParam(const TfToken& paramName) = 0;

    /// Change the value of any parameter on the material.
    ///
    /// This is useful to set default values for parameters before remapping
    /// from existing USD parameters.
    ///
    /// @param paramName Name of the parameter to set.
    /// @param paramValue New value of the parameter wrapped in VtValue.
    virtual void SetParam(const TfToken& paramName, const VtValue& paramValue) = 0;

    /// Change the id of the material.
    ///
    /// This can be used to change the type of the node, ie, change
    /// PxrPreviewSurface to standard_surface as part of the conversion.
    virtual void SetNodeId(const TfToken& nodeId) = 0;

    /// RenameParam's function is to remap a parameter from the USD/HYdra name
    /// to the arnold name and remap connections.
    ///
    /// @param oldParamName The original, USD/Hydra parameter name.
    /// @param newParamName The new, Arnold parameter name.
    virtual void RenameParam(const TfToken& oldParamName, const TfToken& newParamName) = 0;
};

class HydraMaterialEditContext : public MaterialEditContext {
public:
    HydraMaterialEditContext(HdMaterialNetwork& network, HdMaterialNode& node) : _network(network), _node(node) {}

    VtValue GetParam(const TfToken& paramName) override
    {
        const auto paramIt = _node.parameters.find(paramName);
        return paramIt == _node.parameters.end() ? VtValue() : paramIt->second;
    }

    void SetParam(const TfToken& paramName, const VtValue& paramValue) override
    {
        _node.parameters[paramName] = paramValue;
    }

    void SetNodeId(const TfToken& nodeId) override { _node.identifier = nodeId; }

    void RenameParam(const TfToken& oldParamName, const TfToken& newParamName) override
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

using RemapNodeFunc = void (*)(MaterialEditContext*);

RemapNodeFunc _previewSurfaceRemapFunc = [](MaterialEditContext* ctx) {
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

RemapNodeFunc _uvTextureRemapFunc = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_image);
    ctx->RenameParam(str::t_file, str::t_filename);
    ctx->RenameParam(str::t_st, str::t_uvcoords);
    ctx->RenameParam(str::t_fallback, str::t_missing_texture_color);
};

RemapNodeFunc _floatPrimvarRemapFunc = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_float);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t_defaultStr);
};

// Since st and uv is set as the built-in UV parameter on the mesh, we
// have to use a utility node instead of a user_data_rgb node.
RemapNodeFunc _float2PrimvarRemapFunc = [](MaterialEditContext* ctx) {
    const auto varnameValue = ctx->GetParam(str::t_varname);
    if (varnameValue.IsEmpty() || !varnameValue.IsHolding<TfToken>()) {
        return;
    }
    const auto& varname = varnameValue.UncheckedGet<TfToken>();
    // uv and st is remapped to UV coordinates
    if (varname == str::t_uv || varname == str::t_st) {
        // We are reading the uv from the mesh.
        ctx->SetNodeId(str::t_utility);
        ctx->SetParam(str::t_color_mode, VtValue(str::t_uv));
        ctx->SetParam(str::t_shade_mode, VtValue(str::t_flat));
    } else {
        ctx->SetNodeId(str::t_user_data_rgb);
        ctx->RenameParam(str::t_varname, str::t_attribute);
    }
    ctx->RenameParam(str::t_fallback, str::t_defaultStr);
};

RemapNodeFunc _float3PrimvarRemapFunc = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_rgb);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t_defaultStr);
};

RemapNodeFunc _float4PrimvarRemapFunc = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_rgba);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t_defaultStr);
};

RemapNodeFunc _intPrimvarRemapFunc = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_int);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t_defaultStr);
};

RemapNodeFunc _stringPrimvarRemapFunc = [](MaterialEditContext* ctx) {
    ctx->SetNodeId(str::t_user_data_string);
    ctx->RenameParam(str::t_varname, str::t_attribute);
    ctx->RenameParam(str::t_fallback, str::t_defaultStr);
};

const std::unordered_map<TfToken, RemapNodeFunc, TfToken::HashFunctor> _nodeRemapFuncs{
    {str::t_UsdPreviewSurface, _previewSurfaceRemapFunc},
    {str::t_UsdUVTexture, _uvTextureRemapFunc},
    {str::t_UsdPrimvarReader_float, _floatPrimvarRemapFunc},
    {str::t_UsdPrimvarReader_float2, _float2PrimvarRemapFunc},
    {str::t_UsdPrimvarReader_float3, _float3PrimvarRemapFunc},
    {str::t_UsdPrimvarReader_point, _float3PrimvarRemapFunc},
    {str::t_UsdPrimvarReader_normal, _float3PrimvarRemapFunc},
    {str::t_UsdPrimvarReader_vector, _float3PrimvarRemapFunc},
    {str::t_UsdPrimvarReader_float4, _float4PrimvarRemapFunc},
    {str::t_UsdPrimvarReader_int, _intPrimvarRemapFunc},
    {str::t_UsdPrimvarReader_string, _stringPrimvarRemapFunc},
};

void _RemapNetwork(HdMaterialNetwork& network)
{
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
                if (paramIt == material.parameters.end() || !paramIt->second.IsHolding<TfToken>()) {
                    return true;
                }
                const auto& token = paramIt->second.UncheckedGet<TfToken>();
                return token == str::t_uv || token == str::t_st;
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
        const auto remapIt = _nodeRemapFuncs.find(material.identifier);
        if (remapIt == _nodeRemapFuncs.end()) {
            continue;
        }

        HydraMaterialEditContext editCtx(network, material);
        remapIt->second(&editCtx);
    }
}

enum class KatanaShader { katana_constant, katana_default, katana_surface, invalid };

KatanaShader _GetKatanaShaderType(const std::string& code)
{
    constexpr auto katanaConstantName = "katana_constant.glslfx";
    constexpr auto katanaDefaultName = "katana_default.glslfx";
    constexpr auto katanaSurfaceName = "katana_surface.glslfx";
    if (code.find(katanaConstantName) != std::string::npos) {
        return KatanaShader::katana_constant;
    } else if (code.find(katanaDefaultName) != std::string::npos) {
        return KatanaShader::katana_default;
    } else if (code.find(katanaSurfaceName) != std::string::npos) {
        return KatanaShader::katana_surface;
    } else {
        return KatanaShader::invalid;
    }
}

} // namespace

HdArnoldMaterial::HdArnoldMaterial(HdArnoldRenderDelegate* delegate, const SdfPath& id)
    : HdMaterial(id), _delegate(delegate)
{
    _surface = _delegate->GetFallbackShader();
    _volume = _delegate->GetFallbackVolumeShader();
}

HdArnoldMaterial::~HdArnoldMaterial()
{
    for (auto& node : _nodes) {
        AiNodeDestroy(node.second.node);
    }
}

void HdArnoldMaterial::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    const auto id = GetId();
    // Note, Katana 3.2 always dirties the resource, so we don't have to check
    // for dirtyParams or dirtySurfaceShader.
    if ((*dirtyBits & HdMaterial::DirtyResource) && !id.IsEmpty()) {
        param->End();
        auto value = sceneDelegate->GetMaterialResource(GetId());
        AtNode* surfaceEntry = nullptr;
        AtNode* displacementEntry = nullptr;
        AtNode* volumeEntry = nullptr;
        if (value.IsHolding<HdMaterialNetworkMap>()) {
            const auto& map = value.UncheckedGet<HdMaterialNetworkMap>();
#ifdef USD_HAS_NEW_MATERIAL_TERMINAL_TOKENS
            const auto* surfaceNetwork = TfMapLookupPtr(map.map, HdMaterialTerminalTokens->surface);
            const auto* displacementNetwork = TfMapLookupPtr(map.map, HdMaterialTerminalTokens->displacement);
            const auto* volumeNetwork = TfMapLookupPtr(map.map, HdMaterialTerminalTokens->volume);
#else
            const auto* surfaceNetwork = TfMapLookupPtr(map.map, UsdImagingTokens->bxdf);
            decltype(surfaceNetwork) displacementNetwork = nullptr;
            decltype(surfaceNetwork) volumeNetwork = nullptr;
#endif // USD_HAS_NEW_MATERIAL_TERMINAL_TOKENS
            SetNodesUnused();
            auto readNetwork = [&](const HdMaterialNetwork* network) -> AtNode* {
                if (network == nullptr) {
                    return nullptr;
                }
                // We are remapping the preview surface nodes to ones that are supported
                // in Arnold. This way we can keep the export code untouched,
                // and handle connection / node exports separately.
                auto remappedNetwork = *network;
                _RemapNetwork(remappedNetwork);
                return ReadMaterialNetwork(remappedNetwork);
            };
            surfaceEntry = readNetwork(surfaceNetwork);
            displacementEntry = readNetwork(displacementNetwork);
            volumeEntry = readNetwork(volumeNetwork);
            ClearUnusedNodes(surfaceEntry, displacementEntry, volumeEntry);
        } else {
            // Katana 3.2 does not return a HdMaterialNetworkMap for now, but
            // the shader source code. We grab the code and identify which material
            // they use and translate it to standard_surface.
            surfaceEntry = ReadKatana32Material(sceneDelegate, id);
        }
        _surface = surfaceEntry == nullptr ? _delegate->GetFallbackShader() : surfaceEntry;
        _displacement = displacementEntry;
        _volume = volumeEntry == nullptr ? _delegate->GetFallbackVolumeShader() : volumeEntry;
    }
    *dirtyBits = HdMaterial::Clean;
}

HdDirtyBits HdArnoldMaterial::GetInitialDirtyBitsMask() const { return HdMaterial::DirtyResource; }

void HdArnoldMaterial::Reload() {}

AtNode* HdArnoldMaterial::GetSurfaceShader() const { return _surface; }

AtNode* HdArnoldMaterial::GetDisplacementShader() const { return _displacement; }

AtNode* HdArnoldMaterial::GetVolumeShader() const { return _volume; }

AtNode* HdArnoldMaterial::ReadMaterialNetwork(const HdMaterialNetwork& network)
{
    std::vector<AtNode*> nodes;
    nodes.reserve(network.nodes.size());
    for (const auto& node : network.nodes) {
        auto* n = ReadMaterial(node);
        if (n != nullptr) {
            nodes.push_back(n);
        }
    }

    // We have to return the entry point from this function, and there are
    // no hard guarantees that the last node (or the first) is going to be the
    // entry point to the network, so we look for the first node that's not the
    // source to any of the connections.
    for (const auto& relationship : network.relationships) {
        auto* inputNode = FindMaterial(relationship.inputId);
        if (inputNode == nullptr) {
            continue;
        }
        nodes.erase(std::remove(nodes.begin(), nodes.end(), inputNode), nodes.end());
        auto* outputNode = FindMaterial(relationship.outputId);
        if (outputNode == nullptr) {
            continue;
        }
        const auto* outputNodeEntry = AiNodeGetNodeEntry(outputNode);
        if (AiNodeEntryLookUpParameter(outputNodeEntry, relationship.outputName.GetText()) == nullptr) {
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

AtNode* HdArnoldMaterial::ReadMaterial(const HdMaterialNode& material)
{
    const auto* nodeTypeStr = material.identifier.GetText();
    const AtString nodeType(strncmp(nodeTypeStr, "arnold:", 7) == 0 ? nodeTypeStr + 7 : nodeTypeStr);
    TF_DEBUG(HDARNOLD_MATERIAL)
        .Msg("HdArnoldMaterial::ReadMaterial - node %s - type %s\n", material.path.GetText(), nodeType.c_str());
    auto* ret = GetLocalNode(material.path, nodeType);
    if (Ai_unlikely(ret == nullptr)) {
        return nullptr;
    }

    const auto* nentry = AiNodeGetNodeEntry(ret);
    for (const auto& param : material.parameters) {
        const auto& paramName = param.first;
        const auto* pentry = AiNodeEntryLookUpParameter(nentry, AtString(paramName.GetText()));
        if (pentry == nullptr) {
            continue;
        }
        HdArnoldSetParameter(ret, pentry, param.second);
    }
    return ret;
}

AtNode* HdArnoldMaterial::ReadKatana32Material(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    for (auto& it : _nodes) {
        it.second.updated = false;
    }

// Katana 3.2 is using 19.5, so we can turn this off to avoid compilation errors with newer USD builds.
#if USED_USD_VERSION_GREATER_EQ(19, 7)
    return nullptr;
#else
    const auto surfaceCode = sceneDelegate->GetSurfaceShaderSource(id);
    // We are looking for the surface shader's filename, found in
    // <katana_dir>/plugins/Resources/Core/Shaders/Surface.
    // Either katana_constant.glslfx, katana_default.glslfx or
    // katana_surface.glslfx.
    // katana_constant -> constant surface color, emulating it via emission color.
    // katana_default -> default material, with 0.7 diffuse. It also has a
    //  0.1 ambient component but ignoring that.
    // katana_surface -> default material with a texture connected to the diffuse
    //  color slot, specular and a parameter to flip v coordinates. Emulating it
    //  with a default standard_surface and optionally a connected texture.
    const auto shaderType = _GetKatanaShaderType(surfaceCode);
    if (shaderType == KatanaShader::invalid) {
        TF_DEBUG(HDARNOLD_MATERIAL).Msg("\n\tUnsupported shader code received:\n\t%s", surfaceCode.c_str());
        ClearUnusedNodes();
        return nullptr;
    }
    static const SdfPath standardPath("/standard_surface");
    auto* entryPoint = GetLocalNode(standardPath, str::standard_surface);
    if (shaderType == KatanaShader::katana_constant) {
        TF_DEBUG(HDARNOLD_MATERIAL).Msg("\n\tConverting katana_constant to standard surface.");
        AiNodeSetFlt(entryPoint, str::base, 0.0f);
        AiNodeSetFlt(entryPoint, str::specular, 0.0f);
        AiNodeSetFlt(entryPoint, str::emission, 1.0f);
        const auto katanaColorValue = sceneDelegate->GetMaterialParamValue(id, _tokens->katanaColor);
        if (katanaColorValue.IsHolding<GfVec4f>()) {
            const auto& katanaColor = katanaColorValue.UncheckedGet<GfVec4f>();
            AiNodeSetRGB(entryPoint, str::emission_color, katanaColor[0], katanaColor[1], katanaColor[2]);
        }
    } else if (shaderType == KatanaShader::katana_default) {
        TF_DEBUG(HDARNOLD_MATERIAL).Msg("\n\tConverting katana_default to standard surface.");
        AiNodeSetFlt(entryPoint, str::base, 0.7f);
        AiNodeSetFlt(entryPoint, str::specular, 0.0f);
    } else {
        static const SdfPath imagePath("/image");
        TF_DEBUG(HDARNOLD_MATERIAL).Msg("\n\tConverting katana_surface to standard surface.");
        AiNodeSetFlt(entryPoint, str::base, 1.0f);
        AiNodeSetFlt(entryPoint, str::specular, 1.0f);
        const auto diffuseTextureValue = sceneDelegate->GetMaterialParamValue(id, _tokens->diffuseTexture);
        if (diffuseTextureValue.IsHolding<std::string>()) {
            const auto diffuseTexture = diffuseTextureValue.UncheckedGet<std::string>();
            if (!diffuseTexture.empty()) {
                auto* image = GetLocalNode(imagePath, str::image);
                AiNodeSetStr(image, str::filename, diffuseTexture.c_str());
                const auto flipTValue = sceneDelegate->GetMaterialParamValue(id, _tokens->flipT);
                if (flipTValue.IsHolding<int>()) {
                    AiNodeSetBool(image, str::tflip, flipTValue.UncheckedGet<int>() != 0);
                }
                AiNodeLink(image, str::base_color, entryPoint);
            }
        }
    }
    ClearUnusedNodes(entryPoint);
    return entryPoint;
#endif
}

AtNode* HdArnoldMaterial::FindMaterial(const SdfPath& path) const
{
    const auto nodeIt = _nodes.find(path);
    return nodeIt == _nodes.end() ? nullptr : nodeIt->second.node;
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

AtNode* HdArnoldMaterial::GetLocalNode(const SdfPath& path, const AtString& nodeType)
{
    const auto nodeIt = _nodes.find(path);
    // If the node already exists, we are checking if the node type is the same
    // as the requested node type. While this is not meaningful for applications
    // like usdview, which rebuild their scene every in case of changes like this,
    // this is still useful for more interactive applications which keep the
    // render index around for longer times, like Maya to Hydra.
    if (nodeIt != _nodes.end()) {
        if (AiNodeEntryGetNameAtString(AiNodeGetNodeEntry(nodeIt->second.node)) != nodeType) {
            TF_DEBUG(HDARNOLD_MATERIAL).Msg("  existing node found, but type mismatch - deleting old node\n");
            if (nodeIt->second.node != nullptr) {
                AiNodeDestroy(nodeIt->second.node);
            }
            _nodes.erase(nodeIt);
        } else {
            TF_DEBUG(HDARNOLD_MATERIAL).Msg("  existing node found - using it\n");
            nodeIt->second.updated = true;
            if (nodeIt->second.node != nullptr) {
                AiNodeReset(nodeIt->second.node);
            }
            return nodeIt->second.node;
        }
    }
    auto* ret = AiNode(_delegate->GetUniverse(), nodeType);
    _nodes.emplace(path, MaterialData{ret, true});
    if (ret == nullptr) {
        TF_DEBUG(HDARNOLD_MATERIAL).Msg("  unable to create node of type %s - aborting\n", nodeType.c_str());
        return nullptr;
    }
    const auto nodeName = GetLocalNodeName(path);
    AiNodeSetStr(ret, str::name, nodeName);
    return ret;
}

bool HdArnoldMaterial::ClearUnusedNodes(
    const AtNode* surfaceEntryPoint, const AtNode* displacementEntryPoint, const AtNode* volumeEntryPoint)
{
    // We are removing any shaders that has not been updated during material
    // translation.
    // We only have guarantees to erase elements during iteration since C++14.
    std::vector<SdfPath> nodesToRemove;
    for (auto& it : _nodes) {
        if (!it.second.updated) {
            if (it.second.node != nullptr) {
                if (it.second.node == surfaceEntryPoint || it.second.node == displacementEntryPoint ||
                    it.second.node == volumeEntryPoint) {
                    TF_CODING_ERROR(
                        "[HdArnold] Entry point to the material network is not translated! %s",
                        AiNodeGetName(it.second.node));
                    return false;
                }
                AiNodeDestroy(it.second.node);
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
        it.second.updated = false;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
