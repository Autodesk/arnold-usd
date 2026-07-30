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
#include <pxr/base/trace/trace.h>

#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#include <constant_strings.h>
#include "hdarnold.h"
#include "utils.h"

#include <ai.h>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <materials_utils.h>


PXR_NAMESPACE_OPEN_SCOPE

inline void EnsurePathHasMaterialPrefix(SdfPath &path, const SdfPath& materialPath) {
    if (!path.HasPrefix(materialPath)) {
        path = materialPath.AppendPath(path.MakeRelativePath(SdfPath::AbsoluteRootPath()));
    }
}

inline void EnsureMaterialNetworPathsPrefix(HdMaterialNetwork& network, const SdfPath &materialPath)
{
    for (auto& rel : network.relationships) {
        EnsurePathHasMaterialPrefix(rel.inputId, materialPath);
        EnsurePathHasMaterialPrefix(rel.outputId, materialPath);
    }
    for (auto& nd : network.nodes) {
        EnsurePathHasMaterialPrefix(nd.path, materialPath);
    }
}

// Append a suffix to the leaf of a shader prim path, so a re-translated copy of a
// material network produces distinctly-named Arnold nodes (the node names derive
// from these paths - see GetArnoldShaderName). Used to build per-rprim coordinate-
// system variants (see HdArnoldNodeGraph::_BuildCoordSysVariant).
inline void AppendPathLeafSuffix(SdfPath& path, const std::string& suffix)
{
    if (path.IsEmpty() || !path.IsPrimPath())
        return;
    path = path.GetParentPath().AppendChild(TfToken(path.GetName() + suffix));
}

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

    void ConnectShader(
        AtNode* node, const std::string& attrName, const SdfPath& target,
        ArnoldAPIAdapter::ConnectionType type) override
    {
        const std::string targetNodeName = GetArnoldShaderName(target.GetPrimPath(), _nodeGraph.GetId());
        _context.AddConnection(node, attrName.c_str(), targetNodeName.c_str(), type, target.GetElementString());
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
            _renderDelegate->DestroyArnoldNode(node.second);
    }

}

// Root function called to translate a shading NodeGraph primitive
void HdArnoldNodeGraph::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    AiProfileBlock("hydra_proc:HdArnoldNodeGraph:Sync");
    TRACE_FUNCTION();
    if (!_renderDelegate->CanUpdateScene())
        return;

    const auto id = GetId();

    if ((*dirtyBits & HdMaterial::DirtyResource) && !id.IsEmpty()) {
        HdArnoldRenderParamInterrupt param(renderParam);
        const VtValue value = sceneDelegate->GetMaterialResource(GetId());
        bool nodeGraphChanged = false;

        // If primvars:arnold:name is authored and differs from the prim's current
        // path, register the original name so lookups by that string still resolve
        // when the file is referenced and the runtime path was remapped under a
        // different namespace. The token differs by mode: Hydra 1 sees the full
        // attribute name (primvars:arnold:name), Hydra 2 exposes primvars stripped
        // of the "primvars:" prefix (arnold:name). We only do this the first time 
        // this primitive is Synced, which is why we check if nodes were already created
        if (_nodes.empty()) {
            VtValue nameValue = sceneDelegate->Get(id, str::t_primvars_arnold_name);
            if (nameValue.IsEmpty())
                nameValue = sceneDelegate->Get(id, str::t_arnold_name);
            if (nameValue.IsHolding<std::string>()) {
                const std::string &nameStr = nameValue.UncheckedGet<std::string>();
                if (!nameStr.empty() && nameStr != id.GetString()) {
                    _renderDelegate->AddNodeGraphName(nameStr, id);
                }
            }
        }

        if (value.IsHolding<HdMaterialNetworkMap>()) {
            // Do not interrupt the render if this is an imager graph, as imagers
            // can be refreshed independantly of the render itself
            if (!_imagerGraph)
                param.Interrupt();

            const HdMaterialNetworkMap& materialNetworkmap = value.UncheckedGet<HdMaterialNetworkMap>();
            // Before translation starts, we store the previous list of AtNodes
            // for this NodeGraph. After we translated everything, all unused nodes
            // in this list will be destroyed
            _previousNodes = _nodes;

            // Retain the network so per-rprim coordinate-system variants can be
            // re-translated on demand (see _BuildCoordSysVariant). The base claim
            // and the variants (their suffix + remap) are deliberately kept across
            // re-syncs: re-translating resets the base nodes to their pristine
            // "space" and destroys the variant nodes, so we rebuild both further
            // below (_RebuildCoordSysRemaps) from the retained state, reusing the
            // same node names so dependent rprims keep valid, correctly-remapped
            // shader pointers without needing to re-sync.
            _materialNetworkMap = materialNetworkmap;

            // terminals contains the list of terminal node paths
            // whether it's for displacement, surface, volume, etc...
            // As we'll use this to identify the networks root shaders, 
            // we copy the vector and we'll remove elements as we find them.
            // Note that this vector should only have a single or a few elements.
            std::vector<SdfPath> terminals = materialNetworkmap.terminals;
            for (auto &ter :terminals) {
                EnsurePathHasMaterialPrefix(ter, GetId());
            }
            for (const auto& tokenAndMaterialNetwork : materialNetworkmap.map) {
                // terminalType tells us which type of network this is meant to be
                // (surface, displacement, etc...). We're using it to identify a 
                // special case for displacement with UsdPreviewSurface
                const TfToken& terminalType = tokenAndMaterialNetwork.first;

                // network will contain the list of shaders nodes to translate, 
                // as well as the list of relationships (shader connections)
                HdMaterialNetwork network = tokenAndMaterialNetwork.second;
                // If this network doesn't contain any node, then there's nothing to do
                if (network.nodes.empty())
                    continue;
                 // Make sure all paths in the material network are prefixed with the material path
                 // This is due to hydra2 removing the prefix of the materials of the shader nodes
                EnsureMaterialNetworPathsPrefix(network, GetId());

                // Read the material network and retrieve the "root" shader that will referenced
                // from other nodes through one of our terminals. 
                AtNode* node = ReadMaterialNetwork(network, terminalType, terminals);
                AtNode* oldTerminal = nullptr;
                // UpdateTerminal assigns a given shader to a terminal name
                if (node && _nodeGraphCache.UpdateTerminal(
                        terminalType, node, oldTerminal)) {
                    nodeGraphChanged = true;
                }
                
                // Special case for light filters, we need to flush the cache to ensure
                // they're properly updated in Arnold
                if (_wasSyncedOnce && (terminalType == str::color || terminalType.GetString().rfind(
                        "light_filter", 0) == 0)) {
                    nodeGraphChanged = true;
                    AiUniverseCacheFlush(_renderDelegate->GetUniverse(), AI_CACHE_BACKGROUND | AI_CACHE_QUAD);
                }
                if (nodeGraphChanged && node && oldTerminal && oldTerminal != node) {
                    
                    auto replaceOldTerminal = [&](std::unordered_map<std::string, AtNode*> &nodesList) -> bool {
                        for (const auto& n : nodesList) {
                            if (n.second == oldTerminal) {
                                // Tell arnold to replace all links to the previous node with links to the new node
                                AiNodeReplace(oldTerminal, node, false);
                                return true;
                            }
                        }
                        return false;
                    };                        

                    // Search for the node to be replaced in the previous nodes list, 
                    // but also in the new one, in case the old is still part of the shading tree #2568
                    if (!replaceOldTerminal(_previousNodes))
                        replaceOldTerminal(_nodes);
                }
            }
            // Re-establish the coordinate-system remaps on the freshly-translated
            // (pristine) base and rebuild the per-rprim variants, BEFORE the unused-
            // node sweep so the rebuilt variant nodes (recreated under their stored
            // names) are kept rather than deleted.
            _RebuildCoordSysRemaps(sceneDelegate->GetRenderIndex());
            // Loop through previous AtNodes that were created for this node graph.
            // If they're not empty in this list, it means that they're not used anymore.
            // Let's delete the unused ones
            for (const auto& previousNode : _previousNodes) {
                if (previousNode.second) {
                    // Destroy the arnold node
                    _renderDelegate->DestroyArnoldNode(previousNode.second);
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
        // If this node graph is an imager graph, the render won't be interrupted / restarted
        // and instead we just call this render hint that updates the imagers #2452
        if (_imagerGraph)
            AiRenderSetHintBool(_renderDelegate->GetRenderSession(), str::request_imager_update, true);
    }
    *dirtyBits = HdMaterial::Clean;
    _wasSyncedOnce = true;
}

void HdArnoldNodeGraph::RemapCoordSysSpaces(const std::unordered_map<std::string, CoordSysTarget>& remap)
{
    if (remap.empty())
        return;
    for (const auto& entry : _nodes) {
        AtNode* node = entry.second;
        if (node == nullptr || !AiNodeIs(node, str::osl))
            continue;
        // MaterialX geometric nodes (ND_position_vector3, ...) expose their
        // coordinate space as the OSL string input "param_shader_space" (see
        // ReadMtlxOslShader). It has already been rewritten to Arnold's dotted
        // "<name>.<suffix>" form; replace the "<name>" part (the coordinate
        // system name) with the uniquely-named camera node bound to the rprim.
        if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), str::param_shader_space) == nullptr)
            continue;
        const std::string value = AiNodeGetStr(node, str::param_shader_space).c_str();
        if (value.empty())
            continue;
        const size_t dot = value.find('.');
        const std::string name = value.substr(0, dot);
        const auto it = remap.find(name);
        if (it == remap.end())
            continue;
        // Keep the suffix (".camera"/".NDC"/...); a value with no suffix (an
        // unexpected plain name) defaults to the camera space.
        const std::string suffix = (dot == std::string::npos) ? std::string(".camera") : value.substr(dot);
        // Arnold's NDC is Y-opposite to its screen/raster, so the ".NDC" space is
        // resolved through a separate extra-flipped camera when one was created;
        // the other spaces stay on the primary node.
        const std::string& target =
            (suffix == ".NDC" && !it->second.ndcNode.empty()) ? it->second.ndcNode : it->second.node;
        AiNodeSetStr(node, str::param_shader_space, AtString((target + suffix).c_str()));
    }
}

HdDirtyBits HdArnoldNodeGraph::GetInitialDirtyBitsMask() const { return HdMaterial::DirtyResource; }

AtNode* HdArnoldNodeGraph::GetCachedSurfaceShader() const
{
    auto* terminal = _nodeGraphCache.GetTerminal(HdMaterialTerminalTokens->surface);
    return terminal == nullptr ? _renderDelegate->GetFallbackSurfaceShader() : terminal;
}

AtNode* HdArnoldNodeGraph::GetCachedDisplacementShader() const { return _nodeGraphCache.GetTerminal(str::t_displacement); }

AtNode* HdArnoldNodeGraph::GetCachedVolumeShader() const
{
    auto* terminal = _nodeGraphCache.GetTerminal(HdMaterialTerminalTokens->volume);
    return terminal == nullptr ? _renderDelegate->GetFallbackVolumeShader() : terminal;
}

void HdArnoldNodeGraph::_CollectCoordSysNames()
{
    // Capture the coordinate-system names present in this graph from the *base*
    // shader nodes while their "space" inputs are still pristine ("<name>.<suffix>").
    // Called from _RebuildCoordSysRemaps right after (re-)translation and before any
    // remap, so this always reads pristine values. Variant nodes are skipped: they
    // may still carry a previous round's remapped (camera-node) prefix, which would
    // poison the name set.
    const std::unordered_set<std::string> variantNodes = _CoordSysVariantNodeNames();
    _coordSysNamesInGraph.clear();
    for (const auto& entry : _nodes) {
        AtNode* node = entry.second;
        if (node == nullptr || !AiNodeIs(node, str::osl))
            continue;
        if (variantNodes.count(entry.first) != 0)
            continue;
        if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), str::param_shader_space) == nullptr)
            continue;
        const std::string value = AiNodeGetStr(node, str::param_shader_space).c_str();
        if (value.empty())
            continue;
        _coordSysNamesInGraph.insert(value.substr(0, value.find('.')));
    }
}

std::string HdArnoldNodeGraph::_CoordSysSignature(const CoordSysRemap& remap) const
{
    if (_coordSysNamesInGraph.empty())
        return {};
    // Build a deterministic signature from the bindings this graph actually uses,
    // so two rprims binding the same names to the same cameras share one variant.
    std::vector<std::string> parts;
    for (const std::string& name : _coordSysNamesInGraph) {
        const auto it = remap.find(name);
        if (it == remap.end())
            continue;
        parts.push_back(name + ">" + it->second.node + "|" + it->second.ndcNode);
    }
    if (parts.empty())
        return {};
    std::sort(parts.begin(), parts.end());
    std::string signature;
    for (const std::string& part : parts) {
        signature += part;
        signature += ';';
    }
    return signature;
}

std::unordered_set<std::string> HdArnoldNodeGraph::_CoordSysVariantNodeNames() const
{
    std::unordered_set<std::string> names;
    for (const auto* variants : {&_coordSysVariants, &_coordSysRetired}) {
        for (const auto& entry : *variants)
            names.insert(entry.second.nodes.begin(), entry.second.nodes.end());
    }
    return names;
}

bool HdArnoldNodeGraph::_HasCoordSysVariant(const std::string& signature) const
{
    return _coordSysVariants.count(signature) != 0 || _coordSysRetired.count(signature) != 0;
}

int HdArnoldNodeGraph::_CoordSysHoldCount(const std::string& signature) const
{
    const auto it = _coordSysHoldCounts.find(signature);
    return it == _coordSysHoldCounts.end() ? 0 : it->second;
}

void HdArnoldNodeGraph::_DropCoordSysHoldCount(const std::string& signature)
{
    const auto it = _coordSysHoldCounts.find(signature);
    if (it == _coordSysHoldCounts.end())
        return;
    if (--it->second <= 0)
        _coordSysHoldCounts.erase(it);
}

void HdArnoldNodeGraph::_AcquireCoordSysHold(const SdfPath& owner, const std::string& signature)
{
    const auto it = _coordSysHolds.find(owner);
    const std::string previous = (it == _coordSysHolds.end()) ? std::string() : it->second;
    if (previous == signature)
        return;
    if (signature.empty()) {
        if (it != _coordSysHolds.end())
            _coordSysHolds.erase(it);
    } else if (it != _coordSysHolds.end()) {
        it->second = signature;
    } else {
        _coordSysHolds.emplace(owner, signature);
    }
    if (!signature.empty())
        ++_coordSysHoldCounts[signature];
    // Release last, so the retirement below sees the updated counts.
    if (!previous.empty()) {
        _DropCoordSysHoldCount(previous);
        _RetireCoordSysSignature(previous);
    }
}

void HdArnoldNodeGraph::_RetireCoordSysSignature(const std::string& signature)
{
    if (_CoordSysHoldCount(signature) != 0)
        return;
    // The base network is not torn down when its last claim goes: it is the network
    // every other consumer (light filters, rprims with no binding) reads. It is
    // reclaimed in place, and only if a conflicting binding needs the slot.
    if (signature == _baseCoordSysSignature)
        return;
    const auto it = _coordSysVariants.find(signature);
    if (it == _coordSysVariants.end())
        return;
    // Move it out of the rebuild set immediately - that is what stops every later
    // material Sync from re-translating an abandoned network. Its nodes are kept until
    // the next material Sync so that a binding cycling back to this camera revives it.
    _coordSysRetired[signature] = std::move(it->second);
    _coordSysVariants.erase(it);
}

void HdArnoldNodeGraph::_ResetCoordSysBase()
{
    // Nothing retained to re-translate from (the material has not been synced yet),
    // so the base has to keep its current claim.
    if (_materialNetworkMap.map.empty())
        return;
    // Re-translating restores the pristine "space" inputs. CreateArnoldNode reuses
    // the existing nodes by name, so the terminal pointers rprims and light filters
    // already hold stay valid; only the terminals this rebuild produces are updated,
    // leaving any terminal created outside translation (GetOrCreateTerminal) alone.
    const ArnoldNodeGraph rebuilt = _BuildCoordSysVariant(std::string());
    for (const auto& terminal : rebuilt.terminals) {
        AtNode* oldTerminal = nullptr;
        _nodeGraphCache.UpdateTerminal(terminal.first, terminal.second, oldTerminal);
    }
    _baseCoordSysSignature.clear();
    _baseCoordSysRemap.clear();
}

void HdArnoldNodeGraph::_GarbageCollectCoordSysHolds(const HdRenderIndex& renderIndex)
{
    for (auto it = _coordSysHolds.begin(); it != _coordSysHolds.end();) {
        if (renderIndex.GetRprim(it->first) != nullptr) {
            ++it;
            continue;
        }
        const std::string signature = it->second;
        it = _coordSysHolds.erase(it);
        _DropCoordSysHoldCount(signature);
        _RetireCoordSysSignature(signature);
    }
}

void HdArnoldNodeGraph::_DestroyRetiredCoordSysVariants()
{
    for (const auto& variant : _coordSysRetired) {
        for (const std::string& name : variant.second.nodes) {
            const auto it = _nodes.find(name);
            if (it == _nodes.end())
                continue;
            if (it->second != nullptr) {
                // Keep the transient previous-nodes list in sync, so the unused-node
                // sweep at the end of Sync does not destroy the same node twice.
                const auto previousIt = _previousNodes.find(name);
                if (previousIt != _previousNodes.end())
                    previousIt->second = nullptr;
                _renderDelegate->DestroyArnoldNode(it->second);
            }
            _nodes.erase(it);
        }
    }
    _coordSysRetired.clear();
}

void HdArnoldNodeGraph::_RebuildCoordSysRemaps(const HdRenderIndex& renderIndex)
{
    // Called during Sync after the base network is (re-)translated to its pristine
    // state and before the unused-node sweep. Re-establishes the coordinate-system
    // remaps so dependent rprims keep valid, correctly-remapped shader pointers
    // across re-syncs without having to re-sync themselves.
    std::lock_guard<std::mutex> guard(_coordSysMutex);
    // Rprims that no longer exist cannot release their own claims, so collect them
    // here, then free the nodes of everything retired since the last Sync.
    _GarbageCollectCoordSysHolds(renderIndex);
    _DestroyRetiredCoordSysVariants();
    _CollectCoordSysNames();
    // The base was reset to pristine by the (re-)translation; re-apply its remap.
    // RemapCoordSysSpaces only rewrites still-pristine "space" inputs, so this hits
    // only the base nodes (any surviving variant nodes still carry camera prefixes).
    if (!_baseCoordSysSignature.empty())
        RemapCoordSysSpaces(_baseCoordSysRemap);
    // Rebuild each variant one at a time: re-translating recreates its nodes under
    // their stored names (same Arnold pointers, reset to pristine), and remapping
    // immediately after keeps the "only pristine" rule valid - only the just-rebuilt
    // variant is pristine at that moment.
    for (auto& entry : _coordSysVariants) {
        entry.second.nodes.clear();
        entry.second.cache = _BuildCoordSysVariant(entry.second.suffix, &entry.second.nodes);
        RemapCoordSysSpaces(entry.second.remap);
    }
}

HdArnoldNodeGraph::ArnoldNodeGraph HdArnoldNodeGraph::_BuildCoordSysVariant(
    const std::string& suffix, std::vector<std::string>* usedNodes)
{
    // Re-translate the retained network into a fresh set of Arnold nodes by
    // namespacing every shader path with the given unique suffix. ReadMaterialNetwork
    // derives node names (and resolves connections) from these paths, so the variant
    // is fully wired by the same code that builds the base network. Reusing the same
    // suffix across re-syncs recreates the same node names (same Arnold pointers via
    // CreateArnoldNode), so rprims holding a variant terminal stay valid.
    _nodeCaptureList = usedNodes;
    ArnoldNodeGraph cache;
    // Built once outside the loop, exactly as the Sync path does: ReadMaterialNetwork
    // removes each terminal it recognises, so per-network copies would let a later
    // network re-claim a terminal an earlier one already consumed.
    std::vector<SdfPath> terminals = _materialNetworkMap.terminals;
    for (auto& ter : terminals) {
        AppendPathLeafSuffix(ter, suffix);
        EnsurePathHasMaterialPrefix(ter, GetId());
    }
    for (const auto& tokenAndNetwork : _materialNetworkMap.map) {
        const TfToken& terminalType = tokenAndNetwork.first;
        HdMaterialNetwork network = tokenAndNetwork.second; // copy we can namespace
        if (network.nodes.empty())
            continue;
        for (auto& nd : network.nodes)
            AppendPathLeafSuffix(nd.path, suffix);
        for (auto& rel : network.relationships) {
            AppendPathLeafSuffix(rel.inputId, suffix);
            AppendPathLeafSuffix(rel.outputId, suffix);
        }
        EnsureMaterialNetworPathsPrefix(network, GetId());
        AtNode* node = ReadMaterialNetwork(network, terminalType, terminals);
        AtNode* oldTerminal = nullptr;
        if (node)
            cache.UpdateTerminal(terminalType, node, oldTerminal);
    }
    _nodeCaptureList = nullptr;
    return cache;
}

AtNode* HdArnoldNodeGraph::_ResolveCoordSysTerminal(const CoordSysBinding& binding, const TfToken& terminalName)
{
    // Rprims are synced in parallel and share this node graph; serialise the claim
    // bookkeeping, the base claim, the variant build and the remap.
    std::lock_guard<std::mutex> guard(_coordSysMutex);
    const std::string signature = _CoordSysSignature(binding.remap);
    // Move this rprim's claim first: releasing what it held before can free the base
    // slot or retire a variant, which the resolution below then reuses or skips.
    _AcquireCoordSysHold(binding.owner, signature);
    // The graph uses no coordinate system this binding touches: nothing to remap.
    if (signature.empty())
        return _nodeGraphCache.GetTerminal(terminalName);
    // The base is claimed by a binding nobody uses any more (its rprims re-bound or
    // were removed) and this binding needs a different one: take the slot back rather
    // than duplicating the network. Restores the pristine "space" inputs.
    //
    // Not done when this signature already has a variant: the base would then resolve
    // the same thing as that variant, leaving it orphaned - and it could not be freed,
    // because other rprims may still be pointing at its terminal without ever
    // re-syncing. Using the existing variant keeps every claim accounted for; the base
    // slot is reclaimed by whichever signature next needs it.
    if (!_baseCoordSysSignature.empty() && signature != _baseCoordSysSignature &&
        _CoordSysHoldCount(_baseCoordSysSignature) == 0 && !_HasCoordSysVariant(signature))
        _ResetCoordSysBase();
    // First distinct binding claims the base network, remapped in place. This
    // keeps the common case (one binding per material, or a material bound
    // consistently) free of any node duplication.
    if (_baseCoordSysSignature.empty()) {
        RemapCoordSysSpaces(binding.remap);
        _baseCoordSysSignature = signature;
        _baseCoordSysRemap = binding.remap;
        return _nodeGraphCache.GetTerminal(terminalName);
    }
    if (signature == _baseCoordSysSignature)
        return _nodeGraphCache.GetTerminal(terminalName);
    // A conflicting binding: build (once) a re-translated variant and remap it.
    // RemapCoordSysSpaces only rewrites still-pristine "space" inputs, so it
    // touches just this fresh variant - the base and other variants already
    // resolve to their own camera names, which are never coordinate-system names.
    auto it = _coordSysVariants.find(signature);
    if (it == _coordSysVariants.end()) {
        // A variant retired since the last material Sync still has its nodes, already
        // remapped to these cameras: revive it rather than translate a duplicate, so
        // re-binding back and forth does not keep building networks.
        const auto retiredIt = _coordSysRetired.find(signature);
        if (retiredIt != _coordSysRetired.end()) {
            it = _coordSysVariants.emplace(signature, std::move(retiredIt->second)).first;
            _coordSysRetired.erase(retiredIt);
        } else {
            CoordSysVariant variant;
            variant.suffix = "__cs" + std::to_string(++_coordSysVariantCount);
            variant.remap = binding.remap;
            variant.cache = _BuildCoordSysVariant(variant.suffix, &variant.nodes);
            it = _coordSysVariants.emplace(signature, std::move(variant)).first;
            RemapCoordSysSpaces(binding.remap);
        }
    }
    return it->second.cache.GetTerminal(terminalName);
}

AtNode* HdArnoldNodeGraph::GetCachedSurfaceShader(const CoordSysBinding& binding)
{
    auto* terminal = _ResolveCoordSysTerminal(binding, HdMaterialTerminalTokens->surface);
    return terminal == nullptr ? _renderDelegate->GetFallbackSurfaceShader() : terminal;
}

AtNode* HdArnoldNodeGraph::GetCachedDisplacementShader(const CoordSysBinding& binding)
{
    return _ResolveCoordSysTerminal(binding, str::t_displacement);
}

AtNode* HdArnoldNodeGraph::GetCachedVolumeShader(const CoordSysBinding& binding)
{
    auto* terminal = _ResolveCoordSysTerminal(binding, HdMaterialTerminalTokens->volume);
    return terminal == nullptr ? _renderDelegate->GetFallbackVolumeShader() : terminal;
}

AtNode* HdArnoldNodeGraph::GetOrCreateTerminal(HdSceneDelegate* sceneDelegate, const TfToken& terminalName)
{
    // Check if the terminal is cached
    if (_nodeGraphCache.HasTerminal(terminalName)) {
        return _nodeGraphCache.GetTerminal(terminalName);
    } else {
        // Check if the hydra prim has the terminal, create an arnold node and cacheit
        const VtValue value = sceneDelegate->GetMaterialResource(GetId());
        if (value.IsHolding<HdMaterialNetworkMap>()) {
            const HdMaterialNetworkMap& materialNetworkmap = value.UncheckedGet<HdMaterialNetworkMap>();

            auto terminalIt = materialNetworkmap.map.find(terminalName);
            if (terminalIt != materialNetworkmap.map.end()) {
                HdMaterialNetwork network = terminalIt->second;
                if (network.nodes.empty())
                    return nullptr;
                // Make sure the network paths have the material prefix
                EnsureMaterialNetworPathsPrefix(network, GetId());

                auto terminals = materialNetworkmap.terminals;
                for (auto& ter : terminals) {
                    EnsurePathHasMaterialPrefix(ter, GetId());
                }

                AtNode* node = ReadMaterialNetwork(network, terminalName, terminals);
                // UpdateTerminal assigns a given shader to a terminal name
                if (node) {
                    AtNode *oldTerminal = nullptr;
                    if (_nodeGraphCache.UpdateTerminal(terminalName, node, oldTerminal)) {
                        return node;
                    } // else {should already be covered by _nodeGraphCache.GetTerminal(terminalName) }
                }
            }
        }
    }
    return nullptr;
}

AtNode* HdArnoldNodeGraph::GetCachedTerminal(const TfToken& terminalName) const
{
    return _nodeGraphCache.GetTerminal(terminalName);
}

std::vector<AtNode*> HdArnoldNodeGraph::GetCachedTerminals(const TfToken& terminalName)
{
    return _nodeGraphCache.GetTerminals(terminalName);
}

std::vector<AtNode*> HdArnoldNodeGraph::GetOrCreateTerminals(
    HdSceneDelegate* sceneDelegate, const TfToken& terminalPrefix)
{
    std::vector<AtNode*> result;

    // Check if there are any terminals starting with terminalPrefix
    const VtValue value = sceneDelegate->GetMaterialResource(GetId());
    if (value.IsHolding<HdMaterialNetworkMap>()) {
        std::vector<TfToken> foundTerminals;
        const HdMaterialNetworkMap& materialNetworkmap = value.UncheckedGet<HdMaterialNetworkMap>();
        for (const auto& matMap : materialNetworkmap.map) {
            if (matMap.first.GetString().rfind(terminalPrefix.GetString(), 0) == 0)
                foundTerminals.push_back(matMap.first);
        }

        for (const TfToken& terminalName : foundTerminals) {
            AtNode* terminalNode = GetOrCreateTerminal(sceneDelegate, terminalName);
            if (terminalNode)
                result.push_back(terminalNode);
        }
    }
    return result;
}

AtNode* HdArnoldNodeGraph::ReadMaterialNetwork(const HdMaterialNetwork& network, const TfToken& terminalType, std::vector<SdfPath>& terminals)
{
    AiProfileBlock("hydra_proc:HdArnoldNodeGraph:ReadMaterialNetwork");
    TRACE_FUNCTION();
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
        std::string arnoldNodeName = GetArnoldShaderName(nodePath, id);
        AtNode* arnoldNode = ReadShader(arnoldNodeName, node.identifier, inputAttrs, _renderDelegate->GetAPIAdapter(), time, materialReader);
        // Eventually store the root AtNode if it matches the terminal path
        if (node.path == terminalPath) {
            terminalNode = arnoldNode;
            
        }

    }
    // Return the root shader for this shading network
    return terminalNode;
}

HdArnoldNodeGraph* HdArnoldNodeGraph::GetNodeGraph(HdRenderIndex &renderIndex, const SdfPath& id, const HdArnoldRenderDelegate* renderDelegate)
{
    // Since and HdArnoldNodeGraph is used for Material and ArnoldNodeGraph, we need to query both.
    HdArnoldNodeGraph *material = reinterpret_cast<HdArnoldNodeGraph*>(renderIndex.GetSprim(HdPrimTypeTokens->material, id));
    HdArnoldNodeGraph *arnoldNodeGraph = reinterpret_cast<HdArnoldNodeGraph*>(renderIndex.GetSprim(str::t_ArnoldNodeGraph, id));
    HdArnoldNodeGraph *nodeGraph = arnoldNodeGraph ? arnoldNodeGraph : material;

    if (nodeGraph || renderDelegate == nullptr || id.IsEmpty())
        return nodeGraph;
    // Fall back to the render delegate's name map: an ArnoldNodeGraph may have
    // been authored with a source-file path that has since been remapped.
    const SdfPath remapped = renderDelegate->LookupNodeGraphPath(id.GetString());
    if (remapped.IsEmpty() || remapped == id)
        return nullptr;
    return GetNodeGraph(renderIndex, remapped, nullptr);
}

HdArnoldNodeGraph* HdArnoldNodeGraph::GetNodeGraph(HdRenderIndex* renderIndex, const SdfPath& id, const HdArnoldRenderDelegate* renderDelegate)
{
    if (renderIndex) {
        return GetNodeGraph(*renderIndex, id, renderDelegate);
    }
    return nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE
