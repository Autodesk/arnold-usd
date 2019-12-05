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
#include "instancer.h"
#include "material.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (st)
    (uv)
);
// clang-format on

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

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        param->End();
        HdArnoldSetPositionFromPrimvar(_shape.GetShape(), id, delegate, str::vlist);
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        param->End();
        const auto topology = GetMeshTopology(delegate);
        const auto& vertexCounts = topology.GetFaceVertexCounts();
        const auto& vertexIndices = topology.GetFaceVertexIndices();
        const auto numFaces = topology.GetNumFaces();
        const auto numVertexIndices = vertexIndices.size();
        auto* nsides = AiArrayAllocate(numFaces, 1, AI_TYPE_UINT);
        auto* vidxs = AiArrayAllocate(vertexIndices.size(), 1, AI_TYPE_UINT);
        for (auto i = decltype(numFaces){0}; i < numFaces; ++i) {
            AiArraySetUInt(nsides, i, static_cast<unsigned int>(vertexCounts[i]));
        }
        for (auto i = decltype(numVertexIndices){0}; i < numVertexIndices; ++i) {
            AiArraySetUInt(vidxs, i, static_cast<unsigned int>(vertexIndices[i]));
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
        param->End();
        _UpdateVisibility(delegate, dirtyBits);
        AiNodeSetByte(_shape.GetShape(), str::visibility, _sharedData.visible ? AI_RAY_ALL : uint8_t{0});
    }

    if (HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id)) {
        const auto displayStyle = GetDisplayStyle(delegate);
        AiNodeSetByte(
            _shape.GetShape(), str::subdiv_iterations, static_cast<uint8_t>(std::max(0, displayStyle.refineLevel)));
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param->End();
        HdArnoldSetTransform(_shape.GetShape(), delegate, GetId());
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
        param->End();
        arnoldMaterial = queryMaterial();
        assignMaterial(_IsVolume(), arnoldMaterial);
    }

    // TODO: Implement all the primvars.
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        param->End();
        // We are checking if the mesh was changed to volume or vice-versa.
        const auto isVolume = _IsVolume();
        for (const auto& primvar : delegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationConstant)) {
            HdArnoldSetConstantPrimvar(_shape.GetShape(), id, delegate, primvar);
        }
        // The mesh has changed, so we need to reassign materials.
        if (isVolume != _IsVolume()) {
            // Material ID wasn't dirtied, so we should query it.
            if (arnoldMaterial == nullptr) {
                arnoldMaterial = queryMaterial();
            }
            assignMaterial(!isVolume, arnoldMaterial);
        }
        for (const auto& primvar : delegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationUniform)) {
            HdArnoldSetUniformPrimvar(_shape.GetShape(), id, delegate, primvar);
        }
        for (const auto& primvar : delegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationVertex)) {
            if (primvar.name == HdTokens->points) {
                continue;
            } else if (primvar.name == _tokens->st || primvar.name == _tokens->uv) {
                const auto v = delegate->Get(id, primvar.name);
                if (v.IsHolding<VtArray<GfVec2f>>()) {
                    const auto& uv = v.UncheckedGet<VtArray<GfVec2f>>();
                    const auto numUVs = static_cast<unsigned int>(uv.size());
                    // Can assume uvs are flattened, with indices matching
                    // vert indices
                    auto* uvlist = AiArrayConvert(numUVs, 1, AI_TYPE_VECTOR2, uv.data());
                    auto* uvidxs = AiArrayCopy(AiNodeGetArray(_shape.GetShape(), str::vidxs));

                    AiNodeSetArray(_shape.GetShape(), str::uvlist, uvlist);
                    AiNodeSetArray(_shape.GetShape(), str::uvidxs, uvidxs);
                } else {
                    TF_WARN(
                        "[HdArnold] Primvar is named uv/st, but the type is not Vec2f on %s",
                        AiNodeGetName(_shape.GetShape()));
                }
            } else {
                HdArnoldSetVertexPrimvar(_shape.GetShape(), id, delegate, primvar);
            }
        }
        for (const auto& primvar : delegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationFaceVarying)) {
            if (primvar.name == _tokens->st || primvar.name == _tokens->uv) {
                const auto v = delegate->Get(id, primvar.name);
                if (v.IsHolding<VtArray<GfVec2f>>()) {
                    const auto& uv = v.UncheckedGet<VtArray<GfVec2f>>();
                    const auto numUVs = static_cast<unsigned int>(uv.size());
                    // Same memory layout and this data is flattened.
                    auto* uvlist = AiArrayConvert(numUVs, 1, AI_TYPE_VECTOR2, uv.data());
                    auto* uvidxs = AiArrayAllocate(numUVs, 1, AI_TYPE_UINT);
                    for (auto i = decltype(numUVs){0}; i < numUVs; ++i) {
                        AiArraySetUInt(uvidxs, i, i);
                    }
                    AiNodeSetArray(_shape.GetShape(), str::uvlist, uvlist);
                    AiNodeSetArray(_shape.GetShape(), str::uvidxs, uvidxs);
                }
            } else {
                HdArnoldSetFaceVaryingPrimvar(_shape.GetShape(), id, delegate, primvar);
            }
        }
    }

    _shape.Sync(this, *dirtyBits, delegate, param);

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
