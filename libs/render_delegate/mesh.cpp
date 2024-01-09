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
#include "mesh.h"
#include "light.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <mutex>

#include <constant_strings.h>
#include <shape_utils.h>

#include "hdarnold.h"
#include "instancer.h"
#include "node_graph.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (st)
    (uv)
    (catmark)
);
// clang-format on

namespace {

template <typename UsdType, unsigned ArnoldType, typename StorageType>
struct _ConvertValueToArnoldParameter {
    template <typename F>
    inline static void convert(
        AtNode* node, const StorageType& data, const AtString& arnoldName, const AtString& arnoldIndexName, F&& indices,
        const size_t* requiredValues = nullptr)
    {
    }
};

// In most cases we are just receiving a simple VtValue holding one key,
// in this case we simple have to convert the data.
template <typename UsdType, unsigned ArnoldType>
struct _ConvertValueToArnoldParameter<UsdType, ArnoldType, VtValue> {
    template <typename F>
    inline static void convert(
        AtNode* node, const VtValue& value, const AtString& arnoldName, const AtString& arnoldIndexName, F&& indices,
        const size_t* requiredValues = nullptr)
    {
        if (!value.IsHolding<VtArray<UsdType>>()) {
            return;
        }
        const auto& values = value.UncheckedGet<VtArray<UsdType>>();
        if (requiredValues != nullptr && values.size() != *requiredValues) {
            return;
        }
        const auto numValues = static_cast<unsigned int>(values.size());
        // Data comes in as flattened and in these cases the memory layout of the USD data matches the memory layout
        // of the Arnold data.
        auto* valueList = AiArrayConvert(numValues, 1, ArnoldType, values.data());
        AiNodeSetArray(node, arnoldName, valueList);
        AiNodeSetArray(node, arnoldIndexName, indices(numValues));
    }
};

// In other cases, the converted value has to match the number of the keys on the positions
// (like with normals), so we are receiving a sample primvar, and if the keys are less than
// the maximum number of samples, we are copying the first key.
template <typename UsdType, unsigned ArnoldType>
struct _ConvertValueToArnoldParameter<UsdType, ArnoldType, HdArnoldSampledPrimvarType> {
    template <typename F>
    inline static void convert(
        AtNode* node, const HdArnoldSampledPrimvarType& samples, const AtString& arnoldName,
        const AtString& arnoldIndexName, F&& indices, const size_t* requiredValues = nullptr)
    {
        if (samples.count == 0 || samples.values.empty() || !samples.values[0].IsHolding<VtArray<UsdType>>()) {
            return;
        }

        const VtArray<UsdType> *v0 = nullptr;
        if (requiredValues) {
            for (const auto& value : samples.values) {
                const auto& array = value.UncheckedGet<VtArray<UsdType>>();
                if (array.size() == *requiredValues) {
                    v0 = &array;
                    break;
                }
            }
        }
        if (v0 == nullptr)
            return;

        const auto numKeys = static_cast<unsigned int>(samples.count);
        const auto numValues = static_cast<unsigned int>(v0->size());
        auto* valueList = AiArrayAllocate(static_cast<unsigned int>(v0->size()), numKeys, ArnoldType);
        AiArraySetKey(valueList, 0, v0->data());
        for (auto index = decltype(numKeys){1}; index < numKeys; index += 1) {
            if (samples.values.size() > index) {
                const auto& vti = samples.values[index];
                if (ARCH_LIKELY(vti.IsHolding<VtArray<UsdType>>())) {
                    const auto& vi = vti.UncheckedGet<VtArray<UsdType>>();
                    if (vi.size() == v0->size()) {
                        AiArraySetKey(valueList, index, vi.data());
                        continue;
                    }
                }
            }
            AiArraySetKey(valueList, index, v0->data());
        }
        AiNodeSetArray(node, arnoldName, valueList);
        AiNodeSetArray(node, arnoldIndexName, indices(numValues));
    }
};

template <typename UsdType, unsigned ArnoldType, typename StorageType>
inline void _ConvertVertexPrimvarToBuiltin(
    AtNode* node, const StorageType& data, const AtString& arnoldName, const AtString& arnoldIndexName)
{
    // We are receiving per vertex data, the way to support this is in arnold to use the values and copy the vertex ids
    // to the new ids for the given value.
    _ConvertValueToArnoldParameter<UsdType, ArnoldType, StorageType>::convert(
        node, data, arnoldName, arnoldIndexName,
        [&](unsigned int) -> AtArray* { return AiArrayCopy(AiNodeGetArray(node, str::vidxs)); }, nullptr);
}

template <typename UsdType, unsigned ArnoldType, typename StorageType>
inline void _ConvertFaceVaryingPrimvarToBuiltin(
    AtNode* node, const StorageType& data,
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
    const VtIntArray& indices,
#endif
    const AtString& arnoldName, const AtString& arnoldIndexName, const VtIntArray* vertexCounts = nullptr,
    const size_t* vertexCountSum = nullptr)
{
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
    if (!indices.empty()) {
        _ConvertValueToArnoldParameter<UsdType, ArnoldType, StorageType>::convert(
            node, data, arnoldName, arnoldIndexName,
            [&](unsigned int) -> AtArray* { return GenerateVertexIdxs(indices, vertexCounts); });
    } else {
#endif
        _ConvertValueToArnoldParameter<UsdType, ArnoldType, StorageType>::convert(
            node, data, arnoldName, arnoldIndexName,
            [&](unsigned int numValues) -> AtArray* {
                return GenerateVertexIdxs(numValues, vertexCounts, vertexCountSum);
            },
            vertexCountSum);
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
    }
#endif
}

} // namespace

#if PXR_VERSION >= 2102
HdArnoldMesh::HdArnoldMesh(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdArnoldRprim<HdMesh>(str::polymesh, renderDelegate, id)
#else
HdArnoldMesh::HdArnoldMesh(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const SdfPath& instancerId)
    : HdArnoldRprim<HdMesh>(str::polymesh, renderDelegate, id, instancerId)
#endif
{
    // The default value is 1, which won't work well in a Hydra context.
    AiNodeSetByte(GetArnoldNode(), str::subdiv_iterations, 0);
    // polymesh smoothing is disabled by default in arnold core, 
    // but we actually want it to default to true as in the arnold plugins
    AiNodeSetBool(GetArnoldNode(), str::smoothing, true);
}

void HdArnoldMesh::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    TF_UNUSED(reprToken);
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();

    HdArnoldSampledPrimvarType pointsSample;
    const auto dirtyPrimvars = HdArnoldGetComputedPrimvars(sceneDelegate, id, *dirtyBits, _primvars, nullptr, &pointsSample) ||
                               (*dirtyBits & HdChangeTracker::DirtyPrimvar);

    // We need to set the deform keys first if it is specified
    VtValue deformKeysVal = sceneDelegate->Get(id, str::t_deformKeys);
    if (deformKeysVal.IsHolding<int>()) {
        SetDeformKeys(deformKeysVal.UncheckedGet<int>());
    } else {
        SetDeformKeys(-1);
    }

    if (_primvars.count(HdTokens->points) != 0) {
        _numberOfPositionKeys = 1;
    } else if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        param.Interrupt();
        _numberOfPositionKeys = HdArnoldSetPositionFromPrimvar(GetArnoldNode(), id, sceneDelegate, str::vlist, param(), GetDeformKeys(), &_primvars, &pointsSample);
    }

    const auto dirtyTopology = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
    if (dirtyTopology) {
        param.Interrupt();
        const auto topology = GetMeshTopology(sceneDelegate);
        // We have to flip the orientation if it's left handed.
        const auto isLeftHanded = topology.GetOrientation() == PxOsdOpenSubdivTokens->leftHanded;
        _vertexCounts = topology.GetFaceVertexCounts();
        const auto& vertexIndices = topology.GetFaceVertexIndices();
        const auto numFaces = topology.GetNumFaces();
        auto* nsidesArray = AiArrayAllocate(numFaces, 1, AI_TYPE_UINT);
        auto* vidxsArray = AiArrayAllocate(vertexIndices.size(), 1, AI_TYPE_UINT);

        auto* nsides = static_cast<uint32_t*>(AiArrayMap(nsidesArray));
        auto* vidxs = static_cast<uint32_t*>(AiArrayMap(vidxsArray));
        _vertexCountSum = 0;
        // We are manually calculating the sum of the vertex counts here, because we are filtering for negative values
        // from the vertex indices array.
        if (isLeftHanded) {
            for (auto i = decltype(numFaces){0}; i < numFaces; ++i) {
                const auto vertexCount = _vertexCounts[i];
                if (Ai_unlikely(_vertexCounts[i] <= 0)) {
                    nsides[i] = 0;
                    continue;
                }
                nsides[i] = vertexCount;
                for (auto vertex = decltype(vertexCount){0}; vertex < vertexCount; vertex += 1) {
                    vidxs[_vertexCountSum + vertexCount - vertex - 1] =
                        static_cast<uint32_t>(vertexIndices[_vertexCountSum + vertex]);
                }
                _vertexCountSum += vertexCount;
            }
        } else {
            // We still need _vertexCountSum as it is used to validate primvars.
            // We are manually calculating the sum of the vertex counts here, because we are filtering for negative
            // values from the vertex indices array.
            std::transform(_vertexCounts.begin(), _vertexCounts.end(), nsides, [&](const int i) -> uint32_t {
                if (Ai_unlikely(i <= 0)) {
                    return 0;
                }
                const auto vertexCount = static_cast<uint32_t>(i);
                _vertexCountSum += vertexCount;
                return vertexCount;
            });
            std::transform(vertexIndices.begin(), vertexIndices.end(), vidxs, [](const int i) -> uint32_t {
                return Ai_unlikely(i <= 0) ? 0 : static_cast<uint32_t>(i);
            });
            _vertexCounts = {}; // We don't need this anymore.
        }
        AiNodeSetArray(GetArnoldNode(), str::nsides, nsidesArray);
        AiNodeSetArray(GetArnoldNode(), str::vidxs, vidxsArray);
        const auto scheme = topology.GetScheme();
        if (scheme == PxOsdOpenSubdivTokens->catmullClark || scheme == _tokens->catmark) {
            AiNodeSetStr(GetArnoldNode(), str::subdiv_type, str::catclark);
        } else {
            AiNodeSetStr(GetArnoldNode(), str::subdiv_type, str::none);
        }
        AiNodeSetArray(GetArnoldNode(), str::shidxs, HdArnoldGetShidxs(topology.GetGeomSubsets(), numFaces, _subsets));
    }

    CheckVisibilityAndSidedness(sceneDelegate, id, dirtyBits, param);
    if (HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id)) {
        param.Interrupt();
        const auto displayStyle = GetDisplayStyle(sceneDelegate);
        // In Hydra, GetDisplayStyle will return a refine level between [0, 8]. 
        // But this is too much for Arnold subdivision iterations, which will quadruple the amount of polygons 
        // at every iteration. So we're remapping this to be between 0 and 3 (see #931)
        int subdivLevel = (displayStyle.refineLevel <= 0) ? 0 : int(std::log2(float(displayStyle.refineLevel)));
        AiNodeSetByte(
            GetArnoldNode(), str::subdiv_iterations, static_cast<uint8_t>(subdivLevel));
    }

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        HdArnoldSetTransform(GetArnoldNode(), sceneDelegate, GetId());
        transformDirtied = true;
    }

    if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id)) {
        param.Interrupt();
        const auto subdivTags = GetSubdivTags(sceneDelegate);
        ArnoldUsdReadCreases(
            GetArnoldNode(), subdivTags.GetCornerIndices(), subdivTags.GetCornerWeights(),
            subdivTags.GetCreaseIndices(), subdivTags.GetCreaseLengths(), subdivTags.GetCreaseWeights());
    }

    auto materialsAssigned = false;
    auto assignMaterials = [&]() {
        // Materials have already been assigned.
        if (materialsAssigned) {
            return;
        }
        materialsAssigned = true;
        const auto numSubsets = _subsets.size();
        const auto numShaders = numSubsets + 1;
        const auto isVolume = _IsVolume();
        auto* shaderArray = AiArrayAllocate(numShaders, 1, AI_TYPE_POINTER);
        auto* dispMapArray = AiArrayAllocate(numShaders, 1, AI_TYPE_POINTER);
        auto* shader = static_cast<AtNode**>(AiArrayMap(shaderArray));
        auto* dispMap = static_cast<AtNode**>(AiArrayMap(dispMapArray));
        HdArnoldRenderDelegate::PathSetWithDirtyBits nodeGraphs;
        auto setMaterial = [&](const SdfPath& materialId, size_t arrayId) {
            nodeGraphs.insert({materialId, HdChangeTracker::DirtyMaterialId});
            const auto* material = reinterpret_cast<const HdArnoldNodeGraph*>(
                sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));
            if (material == nullptr) {
                shader[arrayId] = isVolume ? GetRenderDelegate()->GetFallbackVolumeShader()
                                           : GetRenderDelegate()->GetFallbackSurfaceShader();
                dispMap[arrayId] = nullptr;
            } else {
                shader[arrayId] = isVolume ? material->GetVolumeShader() : material->GetSurfaceShader();
                dispMap[arrayId] = material->GetDisplacementShader();
            }
        };
        for (auto subset = decltype(numSubsets){0}; subset < numSubsets; ++subset) {
            setMaterial(_subsets[subset], subset);
        }
        setMaterial(sceneDelegate->GetMaterialId(id), numSubsets);
        // Keep track of the materials assigned to this mesh
        GetRenderDelegate()->TrackDependencies(id, nodeGraphs);

        if (std::any_of(dispMap, dispMap + numShaders, [](AtNode* disp) { return disp != nullptr; })) {
            AiArrayUnmap(dispMapArray);
            AiNodeSetArray(GetArnoldNode(), str::disp_map, dispMapArray);
        } else {
            AiArrayUnmap(dispMapArray);
            AiArrayDestroy(dispMapArray);
            AiNodeResetParameter(GetArnoldNode(), str::disp_map);
        }
        AiArrayUnmap(shaderArray);
        AiNodeSetArray(GetArnoldNode(), str::shader, shaderArray);
    };

    if (dirtyPrimvars) {
        HdArnoldGetPrimvars(sceneDelegate, id, *dirtyBits, _numberOfPositionKeys > 1, _primvars);
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        _autobumpVisibilityFlags.ClearPrimvarFlags();
        param.Interrupt();
        const auto isVolume = _IsVolume();
        AtNode *meshLight = _GetMeshLight(sceneDelegate, id);

        for (auto& primvar : _primvars) {
            auto& desc = primvar.second;
            if (!desc.NeedsUpdate()) {
                continue;
            }

            if (desc.interpolation == HdInterpolationConstant) {
                // If we have a mesh light, we want to check for light attributes 
                // with a "light:" namespace
                if (meshLight) {
                    // ignore the attribute arnold:light which is just meant
                    // to trigger the creation of the mesh light
                    if (primvar.first == str::t_arnold_light)
                        continue;

                    std::string primvarStr = primvar.first.GetText();
                    const static std::string s_lightPrefix = "arnold:light:";
                    // check if the attribute starts with "arnold:light:"
                    if (primvarStr.length() > s_lightPrefix.length() && 
                        primvarStr.substr(0, s_lightPrefix.length()) == s_lightPrefix) {
                        // we want to read this attribute and set it in the light node. We need to 
                        // modify the attribute name so that we remove the light prefix
                        primvarStr.erase(7, 6);
                    
                        if (primvarStr == "arnold:shaders") {
                            HdArnoldLight::ComputeLightShaders(sceneDelegate, id, 
                                TfToken("primvars:arnold:light:shaders"), meshLight);
                        } else {
                            HdArnoldSetConstantPrimvar(
                                _geometryLight, TfToken(primvarStr.c_str()), desc.role, desc.value, 
                                nullptr, nullptr, nullptr, _renderDelegate);
                        }
                        continue;
                    }
                }

                HdArnoldSetConstantPrimvar(
                    GetArnoldNode(), primvar.first, desc.role, desc.value, &_visibilityFlags, &_sidednessFlags,
                    &_autobumpVisibilityFlags, _renderDelegate);
            } else if (desc.interpolation == HdInterpolationVertex || desc.interpolation == HdInterpolationVarying) {
                if (primvar.first == _tokens->st || primvar.first == _tokens->uv) {
                    _ConvertVertexPrimvarToBuiltin<GfVec2f, AI_TYPE_VECTOR2>(
                        GetArnoldNode(), desc.value, str::uvlist, str::uvidxs);
                } else if (primvar.first == HdTokens->normals) {
                    HdArnoldSampledPrimvarType sample;
                    sample.count = _numberOfPositionKeys;
                    if (desc.value.IsEmpty()) {
                        sceneDelegate->SamplePrimvar(id, primvar.first, &sample);
                    } else {
                        sample.values.push_back(desc.value);
                        sample.times.push_back(0.f);
                    }
                    _ConvertVertexPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR>(
                            GetArnoldNode(), sample, str::nlist, str::nidxs);
                } else {
                    // If we get to points here, it's a computed primvar, so we need to use a different function.
                    if (primvar.first == HdTokens->points) {
                        HdArnoldSetPositionFromValue(GetArnoldNode(), str::vlist, desc.value);
                    } else {
                        HdArnoldSetVertexPrimvar(GetArnoldNode(), primvar.first, desc.role, desc.value, GetRenderDelegate());
                    }
                }
            } else if (desc.interpolation == HdInterpolationUniform) {
                HdArnoldSetUniformPrimvar(GetArnoldNode(), primvar.first, desc.role, desc.value, GetRenderDelegate());
            } else if (desc.interpolation == HdInterpolationFaceVarying) {
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
                if (primvar.first == _tokens->st || primvar.first == _tokens->uv) {
                    _ConvertFaceVaryingPrimvarToBuiltin<GfVec2f, AI_TYPE_VECTOR2>(
                        GetArnoldNode(), desc.value, desc.valueIndices, str::uvlist, str::uvidxs, &_vertexCounts,
                        &_vertexCountSum);
                } else if (primvar.first == HdTokens->normals) {
                    if (desc.value.IsEmpty()) {
                        HdArnoldIndexedSampledPrimvarType sample;
                        sceneDelegate->SampleIndexedPrimvar(id, primvar.first, &sample);
                        sample.count = _numberOfPositionKeys;
                        _ConvertFaceVaryingPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR, HdArnoldSampledPrimvarType>(
                            GetArnoldNode(), sample, sample.indices.empty() ? VtIntArray{} : sample.indices[0],
                            str::nlist, str::nidxs, &_vertexCounts, &_vertexCountSum);
                    } else {
                        _ConvertFaceVaryingPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR>(
                            GetArnoldNode(), desc.value, desc.valueIndices, str::nlist, str::nidxs, &_vertexCounts,
                            &_vertexCountSum);
                    }
                } else {
                    HdArnoldSetFaceVaryingPrimvar(
                        GetArnoldNode(), primvar.first, desc.role, desc.value, GetRenderDelegate(), desc.valueIndices, &_vertexCounts,
                        &_vertexCountSum);
                }
#else
                if (primvar.first == _tokens->st || primvar.first == _tokens->uv) {
                    _ConvertFaceVaryingPrimvarToBuiltin<GfVec2f, AI_TYPE_VECTOR2>(
                        GetArnoldNode(), desc.value, str::uvlist, str::uvidxs, &_vertexCounts, &_vertexCountSum);
                } else if (primvar.first == HdTokens->normals) {
                    HdArnoldSampledPrimvarType sample;
                    sample.count = _numberOfPositionKeys;
                    if (desc.value.IsEmpty()) {
                        sceneDelegate->SamplePrimvar(id, primvar.first, &sample);
                    } else {
                        sample.values.push_back(desc.value);
                        sample.times.push_back(0.f);
                    }
                    _ConvertVertexPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR>(
                            GetArnoldNode(), sample, str::nlist, str::nidxs);
                } else {
                    HdArnoldSetFaceVaryingPrimvar(
                        GetArnoldNode(), primvar.first, desc.role, desc.value, GetRenderDelegate(), &_vertexCounts, &_vertexCountSum);
                }
#endif
            }
        }

        UpdateVisibilityAndSidedness();
        const auto autobumpVisibility = _autobumpVisibilityFlags.Compose();
        AiNodeSetByte(GetArnoldNode(), str::autobump_visibility, autobumpVisibility);
        // The mesh has changed, so we need to reassign materials.
        if (isVolume != _IsVolume()) {
            assignMaterials();
        }
    }

    // We are forcing reassigning materials if topology is dirty and the mesh has geom subsets.
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId || (dirtyTopology && !_subsets.empty())) {
        param.Interrupt();
        assignMaterials();
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::InitRepr | HdChangeTracker::DirtyPoints |
           HdChangeTracker::DirtyDisplayStyle | HdChangeTracker::DirtyDoubleSided | HdChangeTracker::DirtySubdivTags |
           HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyMaterialId |
           HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyVisibility | HdArnoldShape::GetInitialDirtyBitsMask();
}

bool HdArnoldMesh::_IsVolume() const { return AiNodeGetFlt(GetArnoldNode(), str::step_size) > 0.0f; }

AtNode *HdArnoldMesh::_GetMeshLight(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    bool hasMeshLight = false;
    VtValue lightValue = sceneDelegate->Get(id, str::t_arnold_light);
    if (lightValue.IsHolding<bool>()) {
        hasMeshLight = lightValue.UncheckedGet<bool>();            
    }
    
    if (hasMeshLight) {
        if (_geometryLight == nullptr) {
            // We need to create the mesh light, pointing to the current mesh.
            // We'll name it based on the mesh name, adding a light suffix
            std::string lightName = AiNodeGetName(GetArnoldNode());
            lightName += "/light";
            _geometryLight = _renderDelegate->CreateArnoldNode(str::mesh_light, AtString(lightName.c_str()));
        }
        AiNodeSetPtr(_geometryLight, str::mesh, (void*)GetArnoldNode());
    } else if (_geometryLight) {
        // if a geometry light was previously set and it's not there anymore, 
        // we need to clear it now
        _renderDelegate->DestroyArnoldNode(_geometryLight);
        _geometryLight = nullptr;    
    }
    return _geometryLight;
}

PXR_NAMESPACE_CLOSE_SCOPE
