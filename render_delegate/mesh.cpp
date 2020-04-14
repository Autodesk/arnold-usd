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
#include "mesh.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <mutex>

#include "constant_strings.h"
#include "hdarnold.h"
#include "instancer.h"
#include "material.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (st)
    (uv)
);
// clang-format on

namespace {

template <typename UsdType, unsigned ArnoldType, typename StorageType>
struct _ConvertValueToArnoldParameter {
    inline static unsigned int f(AtNode* node, const StorageType& data, const AtString& arnoldName) { return false; }
};

// In most cases we are just receiving a simple VtValue holding one key,
// in this case we simple have to convert the data.
template <typename UsdType, unsigned ArnoldType>
struct _ConvertValueToArnoldParameter<UsdType, ArnoldType, VtValue> {
    inline static unsigned int f(AtNode* node, const VtValue& value, const AtString& arnoldName)
    {
        if (!value.IsHolding<VtArray<UsdType>>()) {
            return 0;
        }
        const auto& values = value.UncheckedGet<VtArray<UsdType>>();
        const auto numValues = static_cast<unsigned int>(values.size());
        // Data comes in as flattened and in these cases the memory layout of the USD data matches the memory layout
        // of the Arnold data.
        auto* valueList = AiArrayConvert(numValues, 1, ArnoldType, values.data());
        AiNodeSetArray(node, arnoldName, valueList);
        return numValues;
    }
};

// In other cases, the converted value has to match the number of the keys on the positions
// (like with normals), so we are receiving a sample primvar, and if the keys are less than
// the maximum number of samples, we are copying the first key.
template <typename UsdType, unsigned ArnoldType>
struct _ConvertValueToArnoldParameter<UsdType, ArnoldType, HdArnoldSampledPrimvarType> {
    inline static unsigned int f(AtNode* node, const HdArnoldSampledPrimvarType& samples, const AtString& arnoldName)
    {
        if (samples.count == 0 ||
#ifdef USD_HAS_UPDATED_TIME_SAMPLE_ARRAY
            samples.values.empty() ||
#endif
            !samples.values[0].IsHolding<VtArray<UsdType>>()) {
            return 0;
        }
        const auto& v0 = samples.values[0].UncheckedGet<VtArray<UsdType>>();
        const auto numKeys = static_cast<unsigned int>(samples.count);
        const auto numValues = static_cast<unsigned int>(v0.size());
        auto* valueList = AiArrayAllocate(static_cast<unsigned int>(v0.size()), numKeys, ArnoldType);
        AiArraySetKey(valueList, 0, v0.data());
        for (auto index = decltype(numKeys){1}; index < numKeys; index += 1) {
            if (
#ifdef USD_HAS_UPDATED_TIME_SAMPLE_ARRAY
                samples.values.size()
#else
                samples.count
#endif
                > index) {
                const auto& vti = samples.values[index];
                if (ARCH_LIKELY(vti.IsHolding<VtArray<UsdType>>())) {
                    const auto& vi = vti.UncheckedGet<VtArray<UsdType>>();
                    if (vi.size() == v0.size()) {
                        AiArraySetKey(valueList, index, vi.data());
                        continue;
                    }
                }
            }
            AiArraySetKey(valueList, index, v0.data());
        }
        AiNodeSetArray(node, arnoldName, valueList);
        return numValues;
    }
};

template <typename UsdType, unsigned ArnoldType, typename StorageType>
inline void _ConvertVertexPrimvarToBuiltin(
    AtNode* node, const StorageType& data, const AtString& arnoldName, const AtString& arnoldIndexName)
{
    // We are receiving per vertex data, the way to support this is in arnold to use the values and copy the vertex ids
    // to the new ids for the given value.
    if (_ConvertValueToArnoldParameter<UsdType, ArnoldType, StorageType>::f(node, data, arnoldName) == 0) {
        return;
    }
    auto* valueIdxs = AiArrayCopy(AiNodeGetArray(node, str::vidxs));
    AiNodeSetArray(node, arnoldIndexName, valueIdxs);
}

template <typename UsdType, unsigned ArnoldType, typename StorageType>
inline void _ConvertFaceVaryingPrimvarToBuiltin(
    AtNode* node, const StorageType& data, const AtString& arnoldName, const AtString& arnoldIndexName,
    const VtIntArray* vertexCounts = nullptr)
{
    const auto numValues = _ConvertValueToArnoldParameter<UsdType, ArnoldType, StorageType>::f(node, data, arnoldName);
    if (numValues == 0) {
        return;
    }
    AiNodeSetArray(node, arnoldIndexName, HdArnoldGenerateIdxs(numValues, vertexCounts));
}

}

HdArnoldMesh::HdArnoldMesh(HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId)
    : HdMesh(id, instancerId), _shape(str::polymesh, delegate, id, GetPrimId())
{
    // The default value is 1, which won't work well in a Hydra context.
    AiNodeSetByte(_shape.GetShape(), str::subdiv_iterations, 0);
}

void HdArnoldMesh::Sync(
    HdSceneDelegate* delegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    TF_UNUSED(reprToken);
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    const auto& id = GetId();

    const auto dirtyPrimvars = HdArnoldGetComputedPrimvars(delegate, id, *dirtyBits, _primvars) ||
                               (*dirtyBits & HdChangeTracker::DirtyPrimvar);

    if (_primvars.count(HdTokens->points) != 0) {
        _numberOfPositionKeys = 1;
    } else if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        param->Interrupt();
        _numberOfPositionKeys = HdArnoldSetPositionFromPrimvar(_shape.GetShape(), id, delegate, str::vlist);
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        param->Interrupt();
        const auto topology = GetMeshTopology(delegate);
        // We have to flip the orientation if it's left handed.
        const auto isLeftHanded = topology.GetOrientation() == PxOsdOpenSubdivTokens->leftHanded;
        _vertexCounts = topology.GetFaceVertexCounts();
        const auto& vertexIndices = topology.GetFaceVertexIndices();
        const auto numFaces = topology.GetNumFaces();
        const auto numVertexIndices = vertexIndices.size();
        auto* nsides = AiArrayAllocate(numFaces, 1, AI_TYPE_UINT);
        auto* vidxs = AiArrayAllocate(vertexIndices.size(), 1, AI_TYPE_UINT);

        if (isLeftHanded) {
            unsigned int vertexId = 0;
            for (auto i = decltype(numFaces){0}; i < numFaces; ++i) {
                const auto vertexCount = static_cast<unsigned int>(_vertexCounts[i]);
                AiArraySetUInt(nsides, i, vertexCount);
                for (auto vertex = decltype(vertexCount){0}; vertex < vertexCount; vertex += 1) {
                    AiArraySetUInt(
                        vidxs, vertexId + vertexCount - vertex - 1,
                        static_cast<unsigned int>(vertexIndices[vertexId + vertex]));
                }
                vertexId += vertexCount;
            }
        } else {
            for (auto i = decltype(numFaces){0}; i < numFaces; ++i) {
                AiArraySetUInt(nsides, i, static_cast<unsigned int>(_vertexCounts[i]));
            }
            for (auto i = decltype(numVertexIndices){0}; i < numVertexIndices; ++i) {
                AiArraySetUInt(vidxs, i, static_cast<unsigned int>(vertexIndices[i]));
            }
            _vertexCounts = {}; // We don't need this anymore.
        }
        AiNodeSetArray(_shape.GetShape(), str::nsides, nsides);
        AiNodeSetArray(_shape.GetShape(), str::vidxs, vidxs);
        const auto scheme = topology.GetScheme();
        if (scheme == PxOsdOpenSubdivTokens->catmullClark || scheme == PxOsdOpenSubdivTokens->catmark) {
            AiNodeSetStr(_shape.GetShape(), str::subdiv_type, str::catclark);
        } else {
            AiNodeSetStr(_shape.GetShape(), str::subdiv_type, str::none);
        }
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        param->Interrupt();
        _UpdateVisibility(delegate, dirtyBits);
        _shape.SetVisibility(_sharedData.visible ? AI_RAY_ALL : uint8_t{0});
    }

    if (HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id)) {
        const auto displayStyle = GetDisplayStyle(delegate);
        AiNodeSetByte(
            _shape.GetShape(), str::subdiv_iterations, static_cast<uint8_t>(std::max(0, displayStyle.refineLevel)));
    }

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param->Interrupt();
        HdArnoldSetTransform(_shape.GetShape(), delegate, GetId());
        transformDirtied = true;
    }

    if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id)) {
        const auto subdivTags = GetSubdivTags(delegate);
        const auto& cornerIndices = subdivTags.GetCornerIndices();
        const auto& cornerWeights = subdivTags.GetCornerWeights();
        const auto& creaseIndices = subdivTags.GetCreaseIndices();
        const auto& creaseLengths = subdivTags.GetCreaseLengths();
        const auto& creaseWeights = subdivTags.GetCreaseWeights();

        const auto cornerIndicesCount = static_cast<uint32_t>(cornerIndices.size());
        uint32_t cornerWeightCounts = 0;
        for (auto creaseLength : creaseLengths) {
            cornerWeightCounts += std::max(0, creaseLength - 1);
        }

        const auto creaseIdxsCount = cornerIndicesCount * 2 + cornerWeightCounts * 2;
        const auto craseSharpnessCount = cornerIndicesCount + cornerWeightCounts;

        auto* creaseIdxs = AiArrayAllocate(creaseIdxsCount, 1, AI_TYPE_UINT);
        auto* creaseSharpness = AiArrayAllocate(craseSharpnessCount, 1, AI_TYPE_FLOAT);

        uint32_t ii = 0;
        for (auto cornerIndex : cornerIndices) {
            AiArraySetUInt(creaseIdxs, ii * 2, cornerIndex);
            AiArraySetUInt(creaseIdxs, ii * 2 + 1, cornerIndex);
            AiArraySetFlt(creaseSharpness, ii, cornerWeights[ii]);
            ++ii;
        }

        uint32_t jj = 0;
        for (auto creaseLength : creaseLengths) {
            for (auto k = decltype(creaseLength){1}; k < creaseLength; ++k, ++ii) {
                AiArraySetUInt(creaseIdxs, ii * 2, creaseIndices[jj + k - 1]);
                AiArraySetUInt(creaseIdxs, ii * 2 + 1, creaseIndices[jj + k]);
                AiArraySetFlt(creaseSharpness, ii, creaseWeights[jj]);
            }
            jj += creaseLength;
        }

        AiNodeSetArray(_shape.GetShape(), str::crease_idxs, creaseIdxs);
        AiNodeSetArray(_shape.GetShape(), str::crease_sharpness, creaseSharpness);
    }

    auto assignMaterial = [&](bool isVolume, const HdArnoldMaterial* material) {
        if (material != nullptr) {
            AiNodeSetPtr(
                _shape.GetShape(), str::shader, isVolume ? material->GetVolumeShader() : material->GetSurfaceShader());
            AiNodeSetPtr(_shape.GetShape(), str::disp_map, material->GetDisplacementShader());
        } else {
            AiNodeSetPtr(
                _shape.GetShape(), str::shader,
                isVolume ? _shape.GetDelegate()->GetFallbackVolumeShader() : _shape.GetDelegate()->GetFallbackShader());
            AiNodeSetPtr(_shape.GetShape(), str::disp_map, nullptr);
        }
    };

    // Querying material for the second time will return an empty id, so we cache it.
    const HdArnoldMaterial* arnoldMaterial = nullptr;
    auto queryMaterial = [&]() -> const HdArnoldMaterial* {
        return reinterpret_cast<const HdArnoldMaterial*>(
            delegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, delegate->GetMaterialId(id)));
    };
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        param->Interrupt();
        arnoldMaterial = queryMaterial();
        assignMaterial(_IsVolume(), arnoldMaterial);
    }

    if (dirtyPrimvars) {
        HdArnoldGetPrimvars(delegate, id, *dirtyBits, _numberOfPositionKeys > 1, _primvars);
        param->Interrupt();
        const auto isVolume = _IsVolume();
        auto visibility = _shape.GetVisibility();
        for (const auto& primvar : _primvars) {
            const auto& desc = primvar.second;
            if (!desc.dirtied) {
                continue;
            }

            if (desc.interpolation == HdInterpolationConstant) {
                HdArnoldSetConstantPrimvar(_shape.GetShape(), primvar.first, desc.role, desc.value, &visibility);
            } else if (desc.interpolation == HdInterpolationVertex) {
                if (primvar.first == _tokens->st || primvar.first == _tokens->uv) {
                    _ConvertVertexPrimvarToBuiltin<GfVec2f, AI_TYPE_VECTOR2>(
                        _shape.GetShape(), desc.value, str::uvlist, str::uvidxs);
                } else if (primvar.first == HdTokens->normals) {
                    if (desc.value.IsEmpty()) {
                        HdArnoldSampledPrimvarType sample;
                        delegate->SamplePrimvar(id, primvar.first, &sample);
                        sample.count = _numberOfPositionKeys;
                        _ConvertVertexPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR>(
                            _shape.GetShape(), sample, str::nlist, str::nidxs);
                    } else {
                        _ConvertVertexPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR>(
                            _shape.GetShape(), desc.value, str::nlist, str::nidxs);
                    }
                } else {
                    // If we get to points here, it's a computed primvar, so we need to use a different function.
                    if (primvar.first == HdTokens->points) {
                        HdArnoldSetPositionFromValue(_shape.GetShape(), str::vlist, desc.value);
                    } else {
                        HdArnoldSetVertexPrimvar(_shape.GetShape(), primvar.first, desc.role, desc.value);
                    }
                }
            } else if (desc.interpolation == HdInterpolationUniform) {
                HdArnoldSetUniformPrimvar(_shape.GetShape(), primvar.first, desc.role, desc.value);
            } else if (desc.interpolation == HdInterpolationFaceVarying) {
                if (primvar.first == _tokens->st || primvar.first == _tokens->uv) {
                    _ConvertFaceVaryingPrimvarToBuiltin<GfVec2f, AI_TYPE_VECTOR2>(
                        _shape.GetShape(), desc.value, str::uvlist, str::uvidxs, &_vertexCounts);
                } else if (primvar.first == HdTokens->normals) {
                    if (desc.value.IsEmpty()) {
                        HdArnoldSampledPrimvarType sample;
                        delegate->SamplePrimvar(id, primvar.first, &sample);
                        sample.count = _numberOfPositionKeys;
                        _ConvertFaceVaryingPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR>(
                            _shape.GetShape(), sample, str::nlist, str::nidxs, &_vertexCounts);
                    } else {
                        _ConvertFaceVaryingPrimvarToBuiltin<GfVec3f, AI_TYPE_VECTOR>(
                            _shape.GetShape(), desc.value, str::nlist, str::nidxs, &_vertexCounts);
                    }
                } else {
                    HdArnoldSetFaceVaryingPrimvar(
                        _shape.GetShape(), primvar.first, desc.role, desc.value, &_vertexCounts);
                }
            } else if (desc.interpolation == HdInterpolationInstance) {
                // TODO (pal): Add new functions to the instance class to read per instance data.
            }
        }

        _shape.SetVisibility(visibility);
        // The mesh has changed, so we need to reassign materials.
        if (isVolume != _IsVolume()) {
            // Material ID wasn't dirtied, so we should query it.
            if (arnoldMaterial == nullptr) {
                arnoldMaterial = queryMaterial();
            }
            assignMaterial(!isVolume, arnoldMaterial);
        }
    }

    _shape.Sync(this, *dirtyBits, delegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::InitRepr | HdChangeTracker::DirtyPoints |
           HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyMaterialId |
           HdChangeTracker::DirtyPrimID | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyInstanceIndex |
           HdChangeTracker::DirtyVisibility;
}

HdDirtyBits HdArnoldMesh::_PropagateDirtyBits(HdDirtyBits bits) const { return bits & HdChangeTracker::AllDirty; }

void HdArnoldMesh::_InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits)
{
    TF_UNUSED(reprToken);
    TF_UNUSED(dirtyBits);
}

bool HdArnoldMesh::_IsVolume() const { return AiNodeGetFlt(_shape.GetShape(), str::step_size) > 0.0f; }

PXR_NAMESPACE_CLOSE_SCOPE
