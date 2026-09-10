//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Autodesk, Inc.
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
#include "basis_curves.h"
#include <pxr/base/trace/trace.h>

#include <constant_strings.h>
#include <shape_utils.h>

#include "coord_sys.h"
#include "node_graph.h"
#include "utils.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/hash.h>

#include <pxr/usd/sdf/assetPath.h>
#include <ai.h>

/*
 * TODO:
 *  - Add support for per instance variables.
 *  - Investigate periodic and pinned curves.
 *  - Convert normals to orientations.
 *  - Allow overriding basis via a primvar and remap all the parameters.
 *  - Correctly handle degenerate curves using KtoA as an example.
 */

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (pscale)
    ((basis, "arnold:basis"))
    ((orientations, "arnold:orientations"))
);
// clang-format on

namespace {

} // namespace


// Function to convert a GfVec3f array to a GfVec2f array, skipping the last component.
// It could be used more generally later by registering it as a VtValue Cast:
//
//     VtValue::RegisterCast<VtVec3fArray, VtVec2fArray>(&Vec3fToVec2f);
//
//  then simply call the Cast function as follows:
//
//     value = VtValue::Cast<VtVec2fArray>(desc.value);
//
VtValue Vec3fToVec2f(VtValue const &val) {
    if (val.IsHolding<VtVec3fArray>()) {
        const auto& vec3 = val.UncheckedGet<VtVec3fArray>();
        VtVec2fArray vec2(vec3.size());
        std::transform(
            vec3.cbegin(), vec3.cend(), vec2.begin(),
            [](const GfVec3f& in) -> GfVec2f {
                return {in[0], in[1]};
            });
        return VtValue::Take(vec2);
    }
    return {};
}

HdArnoldBasisCurves::HdArnoldBasisCurves(HdArnoldRenderDelegate* delegate, const SdfPath& id)
    : HdArnoldRprim<HdBasisCurves>(str::curves, delegate, id), _interpolation(HdTokens->linear)
{
}

HdArnoldBasisCurves::~HdArnoldBasisCurves()
{
    // Geometry deduplication: if this curve owns a canonical node still referenced by
    // instances, hand it over to the render delegate so it outlives this rprim (see
    // HdArnoldRprim). Only curves that actually registered in the dedup registry pay for this.
    _HandOffDedupOnDestroy();
}

void HdArnoldBasisCurves::Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    AiProfileBlock("hydra_proc:sync:HdArnoldBasisCurves"); 
    TRACE_FUNCTION();
    if (!GetRenderDelegate()->CanUpdateScene())
        return;
 
    TF_UNUSED(reprToken);
    HdArnoldRenderParamInterrupt param(renderParam);
    const auto& id = GetId();
    AtNode* node = GetArnoldNode();

    // If the primitive is invisible for hydra, we want to skip it here
    if (SkipHiddenPrim(sceneDelegate, id, dirtyBits, param))
        return;

    HdArnoldSampledPrimvarType pointsSample;
    bool dirtyTopology = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
    bool dirtyPrimvars = HdArnoldGetComputedPrimvars(sceneDelegate, id, *dirtyBits, _primvars, nullptr, &pointsSample) ||
                               (*dirtyBits & HdChangeTracker::DirtyPrimvar);
    // We need to set the deform keys first if it is specified
    VtValue deformKeysVal = sceneDelegate->Get(id, str::t_deformKeys);
    if (deformKeysVal.IsHolding<int>()) {
        SetDeformKeys(deformKeysVal.UncheckedGet<int>());
    } else {
        SetDeformKeys(-1);
    }
    bool dirtyPoints = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points);
    if (dirtyPrimvars) {
        // This needs to be called before HdArnoldSetPositionFromPrimvar otherwise
        // the velocity primvar might not be present in our list #1994
        HdArnoldGetPrimvars(sceneDelegate, id, *dirtyBits, _primvars);
    }

    // === Geometry deduplication ===
    // Same mechanism as HdArnoldMesh (see the mesh Sync for the full rationale): if this curve
    // is geometrically identical to a previously seen one, share a single canonical Arnold
    // curves node instead of duplicating the geometry (and its BVH). The default "Instances"
    // mode only deduplicates point-instancer prototypes (the flattening case), so plain curves
    // pay no hashing cost; "All" mode also collapses non-instanced duplicates into ginstances.
    // Handles static and deformation-motion-blurred curves; excludes computed/skinned points
    // and velocity/acceleration motion blur.
    if (GetRenderDelegate()->DeduplicateGeometry()) {
        const bool geomDirty = dirtyTopology || dirtyPoints || dirtyPrimvars;
        if (geomDirty) {
            // Populate the instancer id (lazily set by _UpdateInstancer, normally later in
            // SyncShape) on a throwaway copy of the dirty bits so GetInstancerId() is valid here.
            {
                HdDirtyBits instancerDirtyBits = *dirtyBits;
                _UpdateInstancer(sceneDelegate, &instancerDirtyBits);
            }
            const bool instanced = !GetInstancerId().IsEmpty();
            bool eligible = instanced ||
                GetRenderDelegate()->GetGeometryDedupMode() == HdArnoldRenderDelegate::GeometryDedupMode::All;
            HdBasisCurvesTopology dedupTopology;
            uint64_t hash = 0;
            if (eligible) {
                HdArnoldRenderParam* rp = reinterpret_cast<HdArnoldRenderParam*>(_renderDelegate->GetRenderParam());
                const bool computedPoints = _primvars.count(HdTokens->points) != 0;
                eligible = !computedPoints && _primvars.count(HdTokens->velocities) == 0 &&
                           _primvars.count(HdTokens->accelerations) == 0;
                if (eligible && pointsSample.count == 0) {
                    SamplePrimvar(sceneDelegate, id, HdTokens->points, rp->GetShutterRange(), &pointsSample);
                }
                // Deduplicate static and deformation-motion-blurred curves (one or more position
                // keys); every key must hold a point array. Velocity/acceleration blur is
                // excluded above - here we only need the sampled points to match across the shutter.
                eligible = eligible && pointsSample.count >= 1 &&
                           pointsSample.values.size() >= pointsSample.count;
                for (size_t i = 0; eligible && i < pointsSample.count; ++i)
                    eligible = pointsSample.values[i].IsHolding<VtVec3fArray>();
                if (eligible) {
                    dedupTopology = GetBasisCurvesTopology(sceneDelegate);
                    hash = _ComputeGeometryHash(dedupTopology, pointsSample, sceneDelegate, id, instanced);
                }
            }
            // Register/redirect through the shared dedup registry (see HdArnoldRprim). The node
            // may be recreated (ginstance conversion, or reverting to a plain curves node), so
            // refresh the local pointer afterwards.
            _ApplyGeometryDedup(id, eligible, instanced, hash, str::curves, dirtyBits, dirtyPrimvars, param);
            node = GetArnoldNode();
            // _ApplyGeometryDedup may have forced dirty bits (a fresh ginstance to configure, or
            // a revert that needs a full rebuild); refresh the local flags so the blocks below
            // honor them.
            dirtyTopology = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
            dirtyPoints = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points);
            dirtyPrimvars = dirtyPrimvars || (*dirtyBits & HdChangeTracker::DirtyPrimvar);
        }
    }

    TfToken curveType;
    HdBasisCurvesTopology topology;

    if (dirtyTopology || dirtyPoints || dirtyPrimvars) {
        // If topology / points / primvars have changed and this curve has a linear basis
        // then we need to ensure all of these attributes are updated, because
        // Arnold converts linear curves to bezier on the fly #1861
        topology = GetBasisCurvesTopology(sceneDelegate);
        curveType = topology.GetCurveType();
        if (curveType == HdTokens->linear) {
            dirtyTopology = dirtyPoints = dirtyPrimvars = true;
        }
    }

    // Points can either come through accessing HdTokens->points, or driven by UsdSkel.
    // If we already have a primvar for points, it will be translated below, in the
    // primvars conversion section.
    // A deduplicated curve rendered as a ginstance shares its canonical's geometry, so points,
    // topology and geometry primvars are all skipped for it (guarded by !_isInstance below).
    if (dirtyPoints && _primvars.count(HdTokens->points) == 0 && !_isInstance) {
        param.Interrupt();
        HdArnoldSetPositionFromPrimvar(node, id, sceneDelegate, str::points, param(), GetDeformKeys(), &_primvars, &pointsSample);

        // We need to set the widths/radius along with the positions. If they are not set, it will create a default value
        HdArnoldSetRadiusFromPrimvar(node, id, sceneDelegate);
    }

    if (dirtyTopology && !_isInstance) {
        param.Interrupt();
        const auto curveBasis = topology.GetCurveBasis();            
        const auto curveWrap = topology.GetCurveWrap();
        if (curveType == HdTokens->linear) {
            AiNodeSetStr(node, str::basis, str::linear);
            _interpolation = HdTokens->linear;
        } else {
            if (curveBasis == HdTokens->bezier) {
                AiNodeSetStr(node, str::basis, str::bezier);
                _interpolation = HdTokens->bezier;
            } else if (curveBasis == HdTokens->bSpline) {
                AiNodeSetStr(node, str::basis, str::b_spline);
                _interpolation = HdTokens->bSpline;
            } else if (curveBasis == HdTokens->catmullRom) {
                AiNodeSetStr(node, str::basis, str::catmull_rom);
                _interpolation = HdTokens->catmullRom;
            } else {
                AiNodeSetStr(node, str::basis, str::linear);
                _interpolation = HdTokens->linear;
            }
#if ARNOLD_VERSION_NUM >= 70103
            if (curveBasis == HdTokens->bSpline || curveBasis == HdTokens->catmullRom)
                AiNodeSetStr(node, str::wrap_mode, AtString{curveWrap.GetText()});
#endif
        }
        const auto& vertexCounts = topology.GetCurveVertexCounts();
        // When interpolation is linear, we clear out stored vertex counts, because we don't need them anymore.
        // Otherwise we need to store vertex counts for remapping primvars.
        if (_interpolation == HdTokens->linear) {
            decltype(_vertexCounts){}.swap(_vertexCounts);
        } else {
            _vertexCounts = vertexCounts;
        }
        const auto numVertexCounts = vertexCounts.size();
        auto* numPointsArray = AiArrayAllocate(numVertexCounts, 1, AI_TYPE_UINT);
        if (numVertexCounts > 0) {
            auto* numPoints = static_cast<uint32_t*>(AiArrayMap(numPointsArray));
            std::transform(vertexCounts.cbegin(), vertexCounts.cend(), numPoints, [](const int i) -> uint32_t {
                return static_cast<uint32_t>(i);
            });
            AiArrayUnmap(numPointsArray);
        }
        AiNodeSetArray(node, str::num_points, numPointsArray);
    }

    CheckVisibilityAndSidedness(sceneDelegate, id, dirtyBits, param);

    auto transformDirtied = false;
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        param.Interrupt();
        HdArnoldRenderParam * renderParam = reinterpret_cast<HdArnoldRenderParam*>(_renderDelegate->GetRenderParam());
        HdArnoldSetTransform(node, sceneDelegate, GetId(), renderParam->GetShutterRange());
        transformDirtied = true;
    }

    // DirtyCategories carries the coordinate-system bindings, and the material's
    // "space" inputs are rewritten to the cameras bound here, so a binding change
    // has to re-assign the material (see HdArnoldGetCoordSysBinding).
    if (*dirtyBits & (HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtyCategories)) {
        param.Interrupt();
        const auto materialId = sceneDelegate->GetMaterialId(id);
        // Ensure the reference from this shape to its material is properly tracked
        // by the render delegate. A deduplicated curve must also re-sync whenever its
        // canonical changes or is removed; register that dependency here (TrackDependencies
        // replaces the full target list, so it has to go in the same call).
        HdArnoldRenderDelegate::PathSetWithDirtyBits deps {{materialId, HdChangeTracker::DirtyMaterialId}};
        if (_isInstance && !_canonicalPath.IsEmpty())
            deps.insert({_canonicalPath, HdChangeTracker::AllDirty});
        GetRenderDelegate()->TrackDependencies(id, deps);
        auto* material = HdArnoldNodeGraph::GetNodeGraph(sceneDelegate->GetRenderIndex(), materialId, _renderDelegate);
        if (material != nullptr) {
            AiNodeSetPtr(node, str::shader, material->GetCachedSurfaceShader(HdArnoldGetCoordSysBinding(sceneDelegate, id)));
        } else {
            AiNodeSetPtr(node, str::shader, GetRenderDelegate()->GetFallbackSurfaceShader());
        }
    }
    if (*dirtyBits & HdChangeTracker::DirtyCategories) {
        param.Interrupt();
        GetRenderDelegate()->ApplyLightLinking(sceneDelegate, node, id);
    }

    // Geometry primvars (points, widths/radius, orientations, uvs, custom) all live on the
    // curves node; a deduplicated ginstance shares the canonical's, so they are skipped for it
    // (the node-level constant primvars it still needs are applied in the block just below).
    if (dirtyPrimvars && !_isInstance) {
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        param.Interrupt();
        const auto vstep = _interpolation == HdTokens->bezier ? 3 : 1;
        const auto vmin = _interpolation == HdTokens->linear ? 2 : 4;

        ArnoldUsdCurvesData curvesData(vmin, vstep, _vertexCounts);

        // For pinned curves, we might have to remap primvars differently #1240
        bool isPinned = (AiNodeGetStr(node, str::wrap_mode) == str::pinned);

        for (auto& primvar : _primvars) {
            auto& desc = primvar.second;
            if (!desc.NeedsUpdate()) {
                continue;
            }

            if (primvar.first == HdTokens->widths) {
                // For pinned curves, vertex interpolation primvars shouldn't be remapped
                if (((desc.interpolation == HdInterpolationVertex && !isPinned)|| 
                    desc.interpolation == HdInterpolationVarying) &&
                    _interpolation != HdTokens->linear) {
                    auto value = desc.value;
                    curvesData.RemapCurvesVertexPrimvar<float, double, GfHalf>(value);
                    ArnoldUsdCurvesData::SetRadiusFromValue(node, value);
                } else {
                    ArnoldUsdCurvesData::SetRadiusFromValue(node, desc.value);
                }
                continue;
            }

            // The curves node only knows the "uvs" parameter, so we have to rename the attribute
            TfToken arnoldAttributeName = primvar.first;
            auto value = desc.value;
            bool forceDeclare = false;
            if (primvar.first == str::t_uv || primvar.first == str::t_st) {
                arnoldAttributeName = str::t_uvs;
                // Primvars which correspond to an attribute in the arnold node entry will be skipped
                // by default #1961. Here we want to make an exception since uvs is a builtin attribute in Arnold
                // even though it's a primvar in USD
                forceDeclare = true;
                // Special case if the uvs attribute has 3 dimensions
                if (desc.value.IsHolding<VtVec3fArray>()) {
                    value = Vec3fToVec2f(desc.value);
                }
            }
            
            if (desc.interpolation == HdInterpolationConstant) {
                // The number of motion keys has to be matched between points and orientations, so if there are multiple
                // position keys, so we are forcing the user to use the SamplePrimvars function.
                if (primvar.first == _tokens->orientations) {
                    HdArnoldSetNormalsFromPrimvar(node, id, _tokens->orientations, str::orientations, sceneDelegate, param());
                } else if (primvar.first != _tokens->basis) {
                    // We skip reading the basis for now as it would require remapping the vertices, widths and
                    // all the primvars.
                    HdArnoldSetConstantPrimvar(
                        node, arnoldAttributeName, desc.role, value, &_visibilityFlags, &_sidednessFlags,
                        nullptr, GetRenderDelegate());
                }

            } else if (desc.interpolation == HdInterpolationUniform) {
                if (forceDeclare) {
                    DeclareAndAssignParameter(node, arnoldAttributeName, 
                        str::t_uniform, value, GetRenderDelegate()->GetAPIAdapter(), 
                        desc.role == HdPrimvarRoleTokens->color);

                } else {
                    HdArnoldSetUniformPrimvar(node, arnoldAttributeName, desc.role, value, &desc.valueIndices,
                        GetRenderDelegate());
                }
            } else if (desc.interpolation == HdInterpolationVertex || desc.interpolation == HdInterpolationVarying) {
                if (primvar.first == HdTokens->points) {
                    HdArnoldSetPositionFromValue(node, str::points, value);
                } else if (primvar.first == HdTokens->normals) {
                    if (_interpolation == HdTokens->linear)
                        AiMsgWarning("%s : Orientations not supported on linear curves", AiNodeGetName(node));
                    else
                        curvesData.SetOrientationFromValue(node, value);
                } else {
                    if (!desc.valueIndices.empty()) {
                        // This vertex-interpolated primvar is also indexed. Since we might have to remap them 
                        // below with RemapCurvesVertexPrimvar, we first need to flatten the values and get rid 
                        // of the indices                    
                        VtValue flattened;
                        if (FlattenIndexedValue(value, desc.valueIndices, flattened))
                            value = flattened;
                    }
                    // For pinned curves, vertex interpolation primvars shouldn't be remapped
                    if (_interpolation != HdTokens->linear && 
                        !(isPinned && desc.interpolation == HdInterpolationVertex)) {
                        curvesData.RemapCurvesVertexPrimvar<
                            bool, VtUCharArray::value_type, unsigned int, int, float, GfVec2f, GfVec3f, GfVec4f,
                            std::string, TfToken, SdfAssetPath>(value);
                    }
                    if (forceDeclare) {
                        DeclareAndAssignParameter(node, arnoldAttributeName, 
                            str::t_varying, value, GetRenderDelegate()->GetAPIAdapter(), 
                            desc.role == HdPrimvarRoleTokens->color);
                    } else {
                        // we're not assing the valueIndices here as we already flattened the values above
                        // (before the remapping)
                        HdArnoldSetVertexPrimvar(node, arnoldAttributeName, desc.role, value, 
                            nullptr, GetRenderDelegate());
                    }
                }
            }
        }
        UpdateVisibilityAndSidedness();
    }

    // A deduplicated ginstance shares the canonical's geometry, so the geometry primvar block
    // above is skipped for it - but it is still a distinct Arnold node that needs its own
    // node-level state from constant primvars: ray visibility (arnold:visibility), sidedness,
    // matte and any user-data attributes shaders read per instance. Apply just the constant
    // primvars here (orientations/basis and any vertex/uniform/varying primvars belong to the
    // shared geometry and must not be touched). Only the non-instanced ginstance flavor owns
    // such a node; the instanced-prototype flavor renders through the shared canonical.
    if (dirtyPrimvars && _isInstance && _sharedPrototype == nullptr) {
        param.Interrupt();
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        for (auto& primvar : _primvars) {
            auto& desc = primvar.second;
            if (desc.interpolation != HdInterpolationConstant)
                continue;
            if (primvar.first == _tokens->orientations || primvar.first == _tokens->basis)
                continue;
            HdArnoldSetConstantPrimvar(
                node, primvar.first, desc.role, desc.value, &_visibilityFlags, &_sidednessFlags, nullptr,
                GetRenderDelegate());
        }
        UpdateVisibilityAndSidedness();
    }

    // Set motion_start / motion_end if we have a non null shutter range. Only genuine geometry
    // nodes carry a deformation motion range; a ginstance shares its canonical's geometry (and
    // gets its own transform motion blur through the instance matrix), so we skip it there.
    if (!_isInstance) {
        HdArnoldRenderParam * renderParam = reinterpret_cast<HdArnoldRenderParam*>(_renderDelegate->GetRenderParam());
        if (renderParam->GetShutterRange()[0] != renderParam->GetShutterRange()[1]) {
            AiNodeSetFlt(node, str::motion_start, renderParam->GetShutterRange()[0]);
            AiNodeSetFlt(node, str::motion_end, renderParam->GetShutterRange()[1]);
        }
    }

    SyncShape(*dirtyBits, sceneDelegate, param, transformDirtied);

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldBasisCurves::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology |
           HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility | HdChangeTracker::DirtyDoubleSided |
           HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyNormals | HdChangeTracker::DirtyWidths |
           HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtyCategories |
           HdArnoldShape::GetInitialDirtyBitsMask();
}

uint64_t HdArnoldBasisCurves::_ComputeGeometryHash(
    const HdBasisCurvesTopology& topology, const HdArnoldSampledPrimvarType& points, HdSceneDelegate* sceneDelegate,
    const SdfPath& id, bool instanced)
{
    // Curve topology: vertex counts, indices, type, basis and wrap all change the arnold node.
    size_t hash = TfHash::Combine(
        VtValue(topology.GetCurveVertexCounts()).GetHash(), topology.GetCurveType(), topology.GetCurveBasis(),
        topology.GetCurveWrap());
    if (!topology.GetCurveIndices().empty())
        hash = TfHash::Combine(hash, VtValue(topology.GetCurveIndices()).GetHash());
    // Points across the whole shutter: fold in the number of motion keys, each sample time and
    // each sample's values. Two curves are merged only if their deformation is identical at
    // every key - Arnold interpolates points linearly between keys, so matching keys (and times)
    // guarantee matching motion, making the merge exact rather than a current-frame approximation.
    hash = TfHash::Combine(hash, points.count);
    for (size_t i = 0; i < points.count && i < points.values.size(); ++i) {
        if (i < points.times.size())
            hash = TfHash::Combine(hash, points.times[i]);
        if (points.values[i].CanHash())
            hash = TfHash::Combine(hash, points.values[i].GetHash());
    }
    // Every primvar ends up on the curves node (widths/radius, orientations, uvs, custom and
    // constant arnold parameters), so two curves are only interchangeable if all of them match.
    // _primvars is an unordered_map whose iteration order is unspecified, so combine each
    // primvar's own hash commutatively: the result must depend on the set of primvars only,
    // not on the order we happen to walk them in (see HdArnoldMesh::_ComputeGeometryHash).
    size_t primvarsHash = 0;
    for (const auto& primvar : _primvars) {
        size_t ph = TfHash::Combine(primvar.first, static_cast<int>(primvar.second.interpolation));
        if (primvar.second.value.CanHash())
            ph = TfHash::Combine(ph, primvar.second.value.GetHash());
        if (!primvar.second.valueIndices.empty())
            ph = TfHash::Combine(ph, primvar.second.valueIndices);
        primvarsHash += ph;
    }
    hash = TfHash::Combine(hash, primvarsHash);
    // Curves have no displacement or subdivision. For an instanced prototype the shared canonical
    // curves node carries the prototype's own transform and its surface shader (its instancer
    // references it directly, and we merge conservatively on material), so fold both in. This
    // collapses re-rooted point-instancer prototype copies that share the same asset and material.
    if (instanced) {
        const SdfPath materialId = sceneDelegate->GetMaterialId(id);
        HdArnoldNodeGraph* material =
            HdArnoldNodeGraph::GetNodeGraph(sceneDelegate->GetRenderIndex(), materialId, _renderDelegate);
        const auto coordSysBinding = HdArnoldGetCoordSysBinding(sceneDelegate, id);
        const GfMatrix4d xform = sceneDelegate->GetTransform(id);
        hash = TfHash::Combine(hash, xform);
        hash = TfHash::Combine(
            hash, reinterpret_cast<uintptr_t>(material != nullptr ? material->GetCachedSurfaceShader(coordSysBinding)
                                                                  : nullptr));
    }
    // The render tag (usd purpose) drives AiNodeSetDisabled on the shape and is applied outside
    // Sync (UpdateRenderTag), so it cannot be reliably reproduced on a freshly converted
    // ginstance; light-linking categories configure light_group / shadow_group. Fold both in to
    // keep dedup conservative and race-free regardless of which duplicate becomes the canonical.
    hash = TfHash::Combine(hash, sceneDelegate->GetRenderTag(id));
    for (const TfToken& category : sceneDelegate->GetCategories(id)) {
        hash = TfHash::Combine(hash, category);
    }
    return hash;
}

PXR_NAMESPACE_CLOSE_SCOPE
