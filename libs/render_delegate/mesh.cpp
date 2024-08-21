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
#include <iostream>

#include <constant_strings.h>
#include <shape_utils.h>

#include "hdarnold.h"
#include "instancer.h"
#include "node_graph.h"
#include "debugger.h"
#include <numeric>

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
        for (const auto& value : samples.values) {
            if (!value.IsEmpty()) {
                const auto& array = value.UncheckedGet<VtArray<UsdType>>();
                // If an amount of required values is provided, we use the first buffer
                // having the expected size 
                if (requiredValues == nullptr || array.size() == *requiredValues) {
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
    AtNode* node, const StorageType& data, const VtIntArray &indices, const AtString& arnoldName, const AtString& arnoldIndexName)
{
    // We are receiving per vertex data, the way to support this is in arnold to use the values and copy the vertex ids
    // to the new ids for the given value.
    _ConvertValueToArnoldParameter<UsdType, ArnoldType, StorageType>::convert(
        node, data, arnoldName, arnoldIndexName,
        [&](unsigned int) -> AtArray* { return GenerateVertexIdxs(indices, AiNodeGetArray(node, str::vidxs)); }, nullptr);
}

template <typename UsdType, unsigned ArnoldType, typename StorageType>
inline void _ConvertFaceVaryingPrimvarToBuiltin(
    AtNode* node, const StorageType& data,
    const VtIntArray& indices,
    const AtString& arnoldName, const AtString& arnoldIndexName, const VtValue& vertexCountsVtValue,
    const size_t* vertexCountSum = nullptr)
{
    if (!indices.empty()) {
        _ConvertValueToArnoldParameter<UsdType, ArnoldType, StorageType>::convert(
            node, data, arnoldName, arnoldIndexName,
            [&](unsigned int) -> AtArray* { return GenerateVertexIdxs(indices, vertexCountsVtValue.UncheckedGet<VtArray<int>>()); });
    } else {
        _ConvertValueToArnoldParameter<UsdType, ArnoldType, StorageType>::convert(
            node, data, arnoldName, arnoldIndexName,
            [&](unsigned int numValues) -> AtArray* {
                return GenerateVertexIdxs(numValues, vertexCountsVtValue.UncheckedGet<VtArray<int>>(), vertexCountSum);
            },
            vertexCountSum);
    }
}

static void ReleaseArrayCallback(void *data, const void *arr) {
    std::cout << "Destroy array " << arr << " belonging to " << data << std::endl;
    if (data && arr) {
        HdArnoldMesh *mesh = static_cast<HdArnoldMesh *>(data);
        mesh->ReleaseArray(arr);
    }
}


// Compile time mapping of USD type to Arnold types
template<typename T> inline uint32_t GetArnoldTypeFor(const T &) {return AI_TYPE_UNDEFINED;}

// template<> inline uint32_t GetArnoldTypeFor<uint8_t> {return AI_TYPE_BYTE;}
// template<> inline uint32_t GetArnoldTypeFor<VtArray<uint8_t>> {return AI_TYPE_BYTE;}
// template<> inline uint32_t GetArnoldTypeFor<std::vector<uint8_t>> {return AI_TYPE_BYTE;}
// constexpr ??
template<> inline uint32_t GetArnoldTypeFor(const GfVec3f &) {return AI_TYPE_VECTOR;}
template<> inline uint32_t GetArnoldTypeFor(const VtArray<GfVec3f> &) {return AI_TYPE_VECTOR;}
template<> inline uint32_t GetArnoldTypeFor(const std::vector<GfVec3f> &) {return AI_TYPE_VECTOR;}

    //   case AI_TYPE_BYTE:    return ForeignByteArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_INT:     return ForeignIntArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_UINT:    return ForeignUIntArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_BOOLEAN: return ForeignBoolArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_FLOAT:   return ForeignFloatArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_RGB:     return ForeignRGBArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_RGBA:    return ForeignRGBAArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_VECTOR:  return ForeignVectorArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_VECTOR2: return ForeignPoint2Array::allocate(nelements, nkeys, data);
    //   case AI_TYPE_STRING:  return ForeignStringArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_NODE:    return ForeignNodeArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_POINTER: return ForeignPointerArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_ARRAY:   return ForeignArrayArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_MATRIX:  return ForeignMatrixArray::allocate(nelements, nkeys, data);
    //   case AI_TYPE_CLOSURE: return ForeignClosureArray::allocate(nelements, nkeys, data);



template <typename T>
AtArray *CreateAtArrayFromTimeSamples(const HdArnoldSampledPrimvarType &timeSamples, HdMesh *mesh) {
    // Unbox
    HdArnoldSampledType<T> unboxed;
    unboxed.UnboxFrom(timeSamples);

    std::vector<const void *> ptrsToSamples; // SmallVector ??
    for (const auto &val:unboxed.values) {
        ptrsToSamples.push_back(val.data());
    }
    const uint32_t nelements = unboxed.values[0].size(); // TODO make sure that values has something
    const uint32_t type = GetArnoldTypeFor(unboxed.values[0]);
    const uint32_t nkeys = ptrsToSamples.size();
    const void **samples = ptrsToSamples.data();
    return AiArrayUseForeignReadOnlyKeys(nelements, nkeys, type, samples, ReleaseArrayCallback, mesh);
}


int HdArnoldSharePositionFromPrimvar(AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const AtString& paramName,
    const HdArnoldRenderParam* param, int deformKeys = HD_ARNOLD_DEFAULT_PRIMVAR_SAMPLES,
    const HdArnoldPrimvarMap* primvars = nullptr,  HdArnoldSampledPrimvarType *pointsSample = nullptr, HdMesh *mesh=nullptr)
{
   // HdArnoldSampledPrimvarType sample;
    if (pointsSample != nullptr){
        // Always sample
        sceneDelegate->SamplePrimvar(id, HdTokens->points, pointsSample);
        AiNodeSetArray(node, paramName, CreateAtArrayFromTimeSamples<VtVec3fArray>(*pointsSample, mesh));
    }

    // HdArnoldSampledType<VtVec3fArray> xf;
    // HdArnoldUnboxSample(sample, xf);
    // if (xf.count == 0) {
    //     return 0;
    // }
    // const auto& v0 = xf.values[0];
    // if (Ai_unlikely(v0.empty())) {
    //     return 0;
    // }

    // TODO: Check if we can/should extrapolate positions based on velocities/accelerations.
    // const auto extrapolatedCount = 0; //_ExtrapolatePositions(node, paramName, xf, param, deformKeys, primvars);
    // if (extrapolatedCount != 0) {
    //     return extrapolatedCount;
    // }

    // // TODO: varying topology could be checked in the core directly
    // bool varyingTopology = false;
    // for (const auto &value : xf.values) {
    //     if (value.size() != v0.size()) {
    //         varyingTopology = true;
    //         break;
    //     }
    // }
    // if (!varyingTopology) {
    //     auto* arr = AiArrayAllocate(v0.size(), xf.count, AI_TYPE_VECTOR);
    //     for (size_t index = 0; index < xf.count; index++) {
    //         auto t = xf.times[0];
    //         if (xf.count > 1)
    //             t += index * (xf.times[xf.count-1] - xf.times[0]) / (static_cast<float>(xf.count)-1.f);
    //         const auto data = xf.Resample(t);
    //         AiArraySetKey(arr, index, data.data());
    //     }  
    //     AiNodeSetArray(node, paramName, arr);
    //     return xf.count;
    // }

    // // Varying topology, and no velocity. Let's choose which time sample to pick.
    // // Ideally we'd want time = 0, as this is what will correspond to the amount of 
    // // expected vertices in other static arrays (like vertex indices). But we might
    // // not always have this time in our list, so we'll use the first positive time
    // int timeIndex = 0;
    // for (size_t i = 0; i < xf.times.size(); ++i) {
    //     if (xf.times[i] >= 0) {
    //         timeIndex = i;
    //         break;
    //     }
    // }

    // // Let's raise an error as this is going to cause problems during rendering
    // if (xf.count > 1) 
    //     AiMsgError("%-30s | Number of vertices changed between motion steps", AiNodeGetName(node));
    
    // // Just export a single key since the number of vertices change along the shutter range,
    // // and we don't have any velocity / acceleration data
    // auto* arr = AiArrayAllocate(xf.values[timeIndex].size(), 1, AI_TYPE_VECTOR);
    // AiArraySetKey(arr, 0, xf.values[timeIndex].data());
    // AiNodeSetArray(node, paramName, arr);
    return 1;
}

template <typename T>
void _RemapNormalKeys(size_t inputCount, size_t requiredCount, T &sample)
{
    auto origValues = sample.values;
    sample.values.clear();
    sample.times.clear();

    for (size_t t = 0; t < requiredCount; ++t) {
        float remappedInput = (requiredCount > 1) ? 
            float(t) / float(requiredCount - 1) : 0;

        sample.times.push_back(remappedInput);
        remappedInput *= inputCount;
        int floorIndex = (int) remappedInput;
        float remappedDelta = remappedInput - floorIndex;
        if (remappedDelta < AI_EPSILON || floorIndex + 1 >= origValues.size()) {
            // If there's no need to interpolate, we copy the input VtValue for this key
            sample.values.push_back(origValues[std::min(floorIndex, (int)inputCount - 1)]);
        } else {
            // We need to interpolate between 2 keys
            VtValue valueFloor = origValues[floorIndex];
            VtValue valueCeil = origValues[floorIndex + 1];
            if (valueFloor.IsHolding<VtArray<GfVec3f>>() && 
                valueCeil.IsHolding<VtArray<GfVec3f>>()) {
                // Since the VtValues hold an array of vectors, we need to interpolate
                // each of them separately 
                const VtArray<GfVec3f> &normalsFloor = valueFloor.Get<VtArray<GfVec3f>>();
                VtArray<GfVec3f> normalsInterp = normalsFloor;
                
                const VtArray<GfVec3f> &normalsCeil = valueCeil.Get<VtArray<GfVec3f>>();
                if (normalsFloor.size() == normalsCeil.size()) {
                    for (size_t n = 0; n < normalsFloor.size(); ++n) {
                        normalsInterp[n] = (normalsCeil[n] * remappedDelta) +
                            (normalsFloor[n] * (1.f - remappedDelta));
                        normalsInterp[n].Normalize(); // normals need to be normalized
                    }
                } 
                sample.values.push_back(VtValue::Take(normalsInterp));
            }
        }
    }
    sample.count = requiredCount;
}

} // namespace


HdArnoldMesh::HdArnoldMesh(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdArnoldRprim<HdMesh>(str::hdpolymesh, renderDelegate, id)
{
    // The default value is 1, which won't work well in a Hydra context.
    AiNodeSetByte(GetArnoldNode(), str::subdiv_iterations, 0);
    // polymesh smoothing is disabled by default in arnold core, 
    // but we actually want it to default to true as in the arnold plugins
    AiNodeSetBool(GetArnoldNode(), str::smoothing, true);
}

HdArnoldMesh::~HdArnoldMesh() {
    if (_geometryLight) {
        _renderDelegate->UnregisterMeshLight(_geometryLight);
    }
}

void HdArnoldMesh::ReleaseArray(const void *arr) {
    // As we don't have that many member variables let's do a linear search instead of storing the relation
    // between AtArray and VtArray
    if (_vertexCountsVtValue.IsHolding<VtArray<int>>()  
        &&_vertexCountsVtValue.UncheckedGet<VtArray<int>>().data() == arr) {
        _vertexCountsVtValue = VtValue(); // Replace the array by an empty one
    } else if (_vertexIndicesVtValue.IsHolding<VtArray<int>>()
                &&_vertexIndicesVtValue.UncheckedGet<VtArray<int>>().data() == arr) {
        _vertexIndicesVtValue = VtValue();
    } 
    // TODO same with points
}

void HdArnoldMesh::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    if (getenv("TOTO")) WaitForDebugger();
    TF_UNUSED(reprToken);
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();

    //HdArnoldSampledPrimvarType pointsSample;
    const auto dirtyPrimvars = HdArnoldGetComputedPrimvars(sceneDelegate, id, *dirtyBits, _primvars, nullptr, &_pointsSample) ||
                               (*dirtyBits & HdChangeTracker::DirtyPrimvar);

    // We need to set the deform keys first if it is specified
    VtValue deformKeysVal = sceneDelegate->Get(id, str::t_deformKeys);
    if (deformKeysVal.IsHolding<int>()) {
        SetDeformKeys(deformKeysVal.UncheckedGet<int>());
    } else {
        SetDeformKeys(-1);
    }
    AtNode* node = GetArnoldNode();

    if (dirtyPrimvars) {
        // This needs to be called before HdArnoldSetPositionFromPrimvar otherwise
        // the velocity primvar might not be present in our list #1994
        HdArnoldGetPrimvars(sceneDelegate, id, *dirtyBits, _primvars);
    }
    
    if (_primvars.count(HdTokens->points) != 0) {
        _numberOfPositionKeys = 1;
    } else if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        param.Interrupt();
        // TODO
        _numberOfPositionKeys = HdArnoldSharePositionFromPrimvar(GetArnoldNode(), id, sceneDelegate, str::vlist, param(), GetDeformKeys(), &_primvars, &_pointsSample, this);
    }

    const auto dirtyTopology = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
    if (dirtyTopology) {
        param.Interrupt();
        const auto topology = GetMeshTopology(sceneDelegate);
        // We have to flip the orientation if it's left handed, tell arnold to do so
        const auto isLeftHanded = topology.GetOrientation() == PxOsdOpenSubdivTokens->leftHanded;
        AiNodeSetStr(GetArnoldNode(), str::orientation, isLeftHanded ? AtString("leftHanded") : AtString("rightHanded")); // TODO in strings

        // Keep a reference on the buffers as long as this object is live
        const VtIntArray &vertexCounts = topology.GetFaceVertexCounts();
        const VtIntArray &vertexIndices = topology.GetFaceVertexIndices();

        _vertexCountsVtValue = VtValue(vertexCounts);
        _vertexIndicesVtValue = VtValue(vertexIndices);

        _vertexCountSum = std::accumulate(vertexCounts.begin(), vertexCounts.end(), 0);
        // TODO: before casting to uint we need to make sure there are no negative values.
        // Or we could have the core do it and we pass the INT
        AiNodeSetArray(GetArnoldNode(), str::nsides, AiArrayUseForeignReadOnlyBuffer(vertexCounts.size(), AI_TYPE_UINT, vertexCounts.data(), ReleaseArrayCallback, this));
        AiNodeSetArray(GetArnoldNode(), str::vidxs, AiArrayUseForeignReadOnlyBuffer(vertexIndices.size(), AI_TYPE_UINT, vertexIndices.data(), ReleaseArrayCallback, this));

        const auto scheme = topology.GetScheme();
        if (scheme == PxOsdOpenSubdivTokens->catmullClark || scheme == _tokens->catmark) {
            AiNodeSetStr(node, str::subdiv_type, str::catclark);
        } else {
            AiNodeSetStr(node, str::subdiv_type, str::none);
        }

        // TODO share shidx buffer
        const auto numFaces = topology.GetNumFaces();
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
            node, str::subdiv_iterations, static_cast<uint8_t>(subdivLevel));
    }

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        HdArnoldSetTransform(node, sceneDelegate, GetId());
        transformDirtied = true;
    }

    if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id)) {
        param.Interrupt();
        const auto subdivTags = GetSubdivTags(sceneDelegate);
        ArnoldUsdReadCreases(
            node, subdivTags.GetCornerIndices(), subdivTags.GetCornerWeights(),
            subdivTags.GetCreaseIndices(), subdivTags.GetCreaseLengths(), subdivTags.GetCreaseWeights());
    }
    if (*dirtyBits & HdChangeTracker::DirtyCategories) {
        param.Interrupt();
        _renderDelegate->ApplyLightLinking(sceneDelegate, node, id);
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
            AiNodeSetArray(node, str::disp_map, dispMapArray);
        } else {
            AiArrayUnmap(dispMapArray);
            AiArrayDestroy(dispMapArray);
            AiNodeResetParameter(node, str::disp_map);
        }
        AiArrayUnmap(shaderArray);
        AiNodeSetArray(node, str::shader, shaderArray);
    };

    if (dirtyPrimvars) {
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
            //std::cout << "Setting " << GetId().GetString() << " " << primvar.first.GetString() << std::endl;
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
                            HdArnoldLight::ComputeLightShaders(sceneDelegate, _renderDelegate, id, 
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
                    node, primvar.first, desc.role, desc.value, &_visibilityFlags, &_sidednessFlags,
                    &_autobumpVisibilityFlags, _renderDelegate);
            } else if (desc.interpolation == HdInterpolationVertex || desc.interpolation == HdInterpolationVarying) {
                if (primvar.first == _tokens->st || primvar.first == _tokens->uv) {
                    _ConvertVertexPrimvarToBuiltin<GfVec2f, AI_TYPE_VECTOR2>(
                        node, desc.value, desc.valueIndices, str::uvlist, str::uvidxs);
                } else if (primvar.first == HdTokens->normals) {
                    HdArnoldSampledPrimvarType sample;
                    sample.count = _numberOfPositionKeys;
                    VtIntArray arrayIndices;
                    // The number of motion keys has to be matched between points and normals, so if there are multiple
                    // position keys, so we are forcing the user to use the SamplePrimvars function.
                    if (desc.value.IsEmpty() || _numberOfPositionKeys > 1) {
                        sceneDelegate->SamplePrimvar(id, primvar.first, &sample);
                    } else {
                        // HdArnoldSampledPrimvarType will be initialized with 3 samples. 
                        // Here we need to clear them before we push the new description value
                        sample.values.clear();
                        sample.times.clear();
                        sample.values.push_back(desc.value);
                        sample.times.push_back(0.f);
                        sample.count = 1;
                        arrayIndices = desc.valueIndices;
                    }
                    if (sample.count != _numberOfPositionKeys) {
                        _RemapNormalKeys(sample.count, _numberOfPositionKeys, sample);
                    }
                    _ConvertVertexPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR>(
                            node, sample, arrayIndices, str::nlist, str::nidxs);
                } else {
                    // If we get to points here, it's a computed primvar, so we need to use a different function.
                    if (primvar.first == HdTokens->points) {
                        // TODO: share points data
                        HdArnoldSetPositionFromValue(GetArnoldNode(), str::vlist, desc.value);
                    } else {
                        HdArnoldSetVertexPrimvar(node, primvar.first, desc.role, desc.value, GetRenderDelegate());
                    }
                }
            } else if (desc.interpolation == HdInterpolationUniform) {
                HdArnoldSetUniformPrimvar(node, primvar.first, desc.role, desc.value, GetRenderDelegate());
            } else if (desc.interpolation == HdInterpolationFaceVarying) {
                if (primvar.first == _tokens->st || primvar.first == _tokens->uv) {
                    _ConvertFaceVaryingPrimvarToBuiltin<GfVec2f, AI_TYPE_VECTOR2>(
                        GetArnoldNode(), desc.value, desc.valueIndices, str::uvlist, str::uvidxs, _vertexCountsVtValue,
                        &_vertexCountSum);
                } else if (primvar.first == HdTokens->normals) {
                    // The number of motion keys has to be matched between points and normals, so if there are multiple
                    // position keys, so we are forcing the user to use the SamplePrimvars function.
                    if (desc.value.IsEmpty() || _numberOfPositionKeys > 1) {
                        HdArnoldIndexedSampledPrimvarType sample;
                        sample.count = _numberOfPositionKeys;
                        sceneDelegate->SampleIndexedPrimvar(id, primvar.first, &sample);

                        if (sample.count != _numberOfPositionKeys) {
                           _RemapNormalKeys(sample.count, _numberOfPositionKeys, sample);
                        }
                        _ConvertFaceVaryingPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR, HdArnoldSampledPrimvarType>(
                            GetArnoldNode(), sample, sample.indices.empty() ? VtIntArray{} : sample.indices[0],
                            str::nlist, str::nidxs, _vertexCountsVtValue, &_vertexCountSum);
                    } else {
                        _ConvertFaceVaryingPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR>(
                            GetArnoldNode(), desc.value, desc.valueIndices, str::nlist, str::nidxs, _vertexCountsVtValue,
                            &_vertexCountSum);
                    }
                } else {
                    const VtIntArray &vertexCounts = _vertexCountsVtValue.UncheckedGet<VtIntArray>();
                    HdArnoldSetFaceVaryingPrimvar(
                        GetArnoldNode(), primvar.first, desc.role, desc.value, GetRenderDelegate(), desc.valueIndices, &vertexCounts,
                        &_vertexCountSum);
                }
            }
        }

        UpdateVisibilityAndSidedness();
        const auto autobumpVisibility = _autobumpVisibilityFlags.Compose();
        AiNodeSetByte(node, str::autobump_visibility, autobumpVisibility);
        // The mesh has changed, so we need to reassign materials.
        if (isVolume != _IsVolume()) {
            assignMaterials();
        }
    
        // As it's done in the procedural for #679, we want to disable subdivision
        // if subdiv iterations is equal to 0
        if (AiNodeGetInt(node, str::subdiv_iterations) == 0) {
            AiNodeSetStr(node, str::subdiv_type, str::none);
        }        
    }

    // We are forcing reassigning materials if topology is dirty and the mesh has geom subsets.
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId || (dirtyTopology && !_subsets.empty())) {
        param.Interrupt();
        assignMaterials();
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);

    // Testing the use of the scatter mode
   // SyncScatteredInstances(sceneDelegate);
    auto instancerId = GetInstancerId();
    auto& renderIndex = sceneDelegate->GetRenderIndex();
    auto* instancer = static_cast<HdArnoldInstancer*>(renderIndex.GetInstancer(instancerId));
    if (instancer) {
        instancer->ComputeMeshInstances(_renderDelegate, GetId(), GetArnoldNode());
    }


    *dirtyBits = HdChangeTracker::Clean;
}


void HdArnoldMesh::SyncScatteredInstances(HdSceneDelegate* sceneDelegate)
{
    // Get the instancers for this mesh
    auto instancerId = GetInstancerId();
    auto& renderIndex = sceneDelegate->GetRenderIndex();
    auto* instancer = static_cast<HdArnoldInstancer*>(renderIndex.GetInstancer(instancerId));
    auto prototypeId = GetId(); // Assuming this node is the prototype
    
    HdArnoldSampledType<GfMatrix4d> instancerTransforms;
    sceneDelegate->SampleInstancerTransform(instancerId, &instancerTransforms);

    // TODO get matrices from instancer
    const auto instanceIndices = sceneDelegate->GetInstanceIndices(instancerId, prototypeId);
    auto numInstances = instanceIndices.size();
    std::vector<AtMatrix> matrixVector;
    for (auto instance = decltype(numInstances){0}; instance < numInstances; instance += 1) {
        const auto instanceIndex = instanceIndices[instance];
//         auto matrix = instancerTransform;
//         if (translates.size() > static_cast<size_t>(instanceIndex)) {
//             GfMatrix4d m(1.0);
//             GfVec3f translate = translates[instanceIndex];
//             // For velocity blur, we add the velocity and/or acceleration 
//             // to the current position
//             if (hasVelocities) {
//                 translate += velocities[instanceIndex] * fps * t;
//             }
//             if (hasAccelerations) {
//                 translate += accelerations[instanceIndex] * fps2 * t2 * 0.5f;
//             }
//             m.SetTranslate(translate);
//             matrix = m * matrix;
//         }
//         if (rotates.size() > static_cast<size_t>(instanceIndex)) {
//             GfMatrix4d m(1.0);
// #if PXR_VERSION >= 2008
//             m.SetRotate(rotates[instanceIndex]);
// #else
//             const auto quat = rotates[instanceIndex];
//             m.SetRotate(GfRotation(GfQuaternion(quat[0], GfVec3f(quat[1], quat[2], quat[3]))));
// #endif
//             matrix = m * matrix;
//         }
//         if (scales.size() > static_cast<size_t>(instanceIndex)) {
//             GfMatrix4d m(1.0);
//             m.SetScale(scales[instanceIndex]);
//             matrix = m * matrix;
//         }
//         if (transforms.size() > static_cast<size_t>(instanceIndex)) {
//             matrix = transforms[instanceIndex] * matrix;
//         }
//         sampleArray.values[sample][instance] = matrix;
    }
    // std::vector<AtMatrix> matrixVector;
    // AtArray *matrices = AiArrayAllocate(10, 1, AI_TYPE_MATRIX);
    // for (unsigned int k=0; k<10; ++k) {
    //     int i = k / 2;
    //     int j = k % 2;
    //     float x = i*50.f;
    //     float y = j*50.f;
    //     matrixVector.push_back(AiM4Translation({x,y,0.f}));
    // }
    // AiArraySetKey(matrices, 0, matrixVector.data());

    // AiNodeSetArray(GetArnoldNode(), str::instance_matrix, matrices);





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
        _renderDelegate->RegisterMeshLight(_geometryLight);
    } else if (_geometryLight) {
        // if a geometry light was previously set and it's not there anymore, 
        // we need to unregister and clear it now
        _renderDelegate->UnregisterMeshLight(_geometryLight);
        _renderDelegate->DestroyArnoldNode(_geometryLight);
        _geometryLight = nullptr;    
    }
    return _geometryLight;
}

PXR_NAMESPACE_CLOSE_SCOPE
