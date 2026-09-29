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
/// @file rprim.h
///
/// Utilities for handling common rprim behavior.
#pragma once

#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/rprim.h>

#include <pxr/base/tf/hash.h>

#include <constant_strings.h>

#include "coord_sys.h"
#include "render_delegate.h"
#include "shape.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

template <typename HydraType>
class HdArnoldRprim : public HydraType {
public:
    /// Constructor for HdArnoldRprim.
    ///
    /// @param shapeType AtString storing the type of the Arnold Shape node.
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the primitive.
    HDARNOLD_API
    HdArnoldRprim(const AtString& shapeType, HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
        : HydraType(id), _shape(shapeType, renderDelegate, id, HydraType::GetPrimId()), _renderDelegate(renderDelegate)
    {
    }

    /// Destructor for HdArnoldRprim.
    ///
    /// Frees the shape and all the ginstances created.
    ~HdArnoldRprim() override {_renderDelegate->ClearDependencies(HydraType::GetId());}

    /// Gets the Arnold Shape.
    ///
    /// @return Reference to the Arnold Shape.
    HdArnoldShape& GetShape() { return _shape; }
    /// Gets the Arnold Shape.
    ///
    /// @return Constant reference to the Arnold Shape.
    const HdArnoldShape& GetShape() const { return _shape; }
    /// Gets the Arnold Node from the shape.
    ///
    /// @return Pointer to the Arnold Node.
    AtNode* GetArnoldNode() { return _shape.GetShape(); }
    /// Gets the Arnold Node from the shape.
    ///
    /// @return Pointer to the Arnold Node.
    const AtNode* GetArnoldNode() const { return _shape.GetShape(); }
    /// Gets the Render Delegate.
    ///
    /// @return Pointer to the Render Delegate.
    HdArnoldRenderDelegate* GetRenderDelegate() { return _renderDelegate; }

#if PXR_VERSION >= 2203
    /// Tracking render tag changes
    void UpdateRenderTag(HdSceneDelegate *delegate, HdRenderParam *renderParam) override {
        HdRprim::UpdateRenderTag(delegate, renderParam);
        HdArnoldRenderParamInterrupt param(renderParam);
        _shape.UpdateRenderTag(this, delegate, param);
    }
#endif

    /// Syncs internal data and arnold state with hydra.
    void SyncShape(
        HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParamInterrupt& param, bool force = false)
    {
        // Newer USD versions need to update the instancer before accessing the instancer id.
        HydraType::_UpdateInstancer(sceneDelegate, &dirtyBits);
        // We also force syncing of the parent instancers.
        HdInstancer::_SyncInstancerAndParents(sceneDelegate->GetRenderIndex(), HydraType::GetInstancerId());
        _shape.Sync(this, dirtyBits, sceneDelegate, param, force);
    }

    bool SkipHiddenPrim(HdSceneDelegate* sceneDelegate, const SdfPath& id, HdDirtyBits* dirtyBits, HdArnoldRenderParamInterrupt& param)
    {
        if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id))
            HydraType::_UpdateVisibility(sceneDelegate, dirtyBits);

        // If this geometry isn't visible, we want to disable it and skip the translation
        bool skip = !this->_sharedData.visible;
        if (skip) {
            // If we're about to skip this prim, we want to clean 
            // its dirtyBits, so that the next modification triggers a new Sync #2467
            *dirtyBits = HdChangeTracker::Clean;
        } else if (_skipped) {
            // This prim was previously skipped and is now visible. Since the translation
            // in previous iterations was bypassed, we must now ensure that
            // it will be fully translated to Arnold #2467
            *dirtyBits = HdChangeTracker::AllDirty;
        }
        _skipped = skip; // Remember if this prim was skipped for next iteration
        if (GetArnoldNode() == nullptr)
            return skip;

        const bool wasHidden = _shape.IsHidden();
        if (skip == wasHidden)
            return skip;

        if (wasHidden) {
            // We're about to turn this disabled node into an active one.
            // But we must ensure it hadn't been disabled due to its render tags.
            // If so, we don't want to stop the render nor change its state
            if (!_renderDelegate->IsVisibleRenderTag(sceneDelegate->GetRenderTag(id)))
                return false;
        }
        param.Interrupt();
        // A deduplication canonical can be instanced by other rprims.
        _shape.SetHidden(skip, _dedupHash != 0 && !_IsDuplicate());

        return skip;
    }
    
    /// Checks if the visibility and sidedness has changed and applies it to the shape. Interrupts the rendering if
    /// either has changed.
    ///
    /// @param sceneDelegate Pointer to the Hydra Scene Delegate
    /// @param id Path of the primitive.
    /// @param dirtyBits Pointer to the Hydra dirty bits of the shape.
    /// @param param Utility to interrupt rendering.
    void CheckVisibilityAndSidedness(
        HdSceneDelegate* sceneDelegate, const SdfPath& id, HdDirtyBits* dirtyBits, HdArnoldRenderParamInterrupt& param, bool checkSidedness = true)
    {
        if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
            param.Interrupt();
            HydraType::_UpdateVisibility(sceneDelegate, dirtyBits);
            _visibilityFlags.SetHydraFlag(this->_sharedData.visible ? AI_RAY_ALL : 0);
            _shape.SetVisibility(this->_sharedData.visible ? _visibilityFlags.Compose() : 0);
        }

        
        if (checkSidedness && HdChangeTracker::IsDoubleSidedDirty(*dirtyBits, id)) {
            param.Interrupt();
            bool doubleSided = sceneDelegate->GetDoubleSided(id);
#if ARNOLD_VERSION_NUM >= 70500
            // For arnold 7.5.0 and up, the option's attribute usd_override_double_sided
            // tells us if we should consider doubleSided or ignore it (#2099)
            if (AiNodeGetBool(AiUniverseGetOptions(_renderDelegate->GetUniverse()), str::usd_override_double_sided))
                doubleSided = true;
#endif
            _sidednessFlags.SetHydraFlag(doubleSided ? AI_RAY_ALL : AI_RAY_SUBSURFACE);
            AiNodeSetByte(GetArnoldNode(), str::sidedness, _sidednessFlags.Compose());
        }
    }
    /// Updates the visibility and sidedness parameters on a mesh. This should be used after primvars have been
    /// updated.
    void UpdateVisibilityAndSidedness()
    {        
        _shape.SetVisibility(this->_sharedData.visible ? _visibilityFlags.Compose() : 0);
        AiNodeSetByte(GetArnoldNode(), str::sidedness, _sidednessFlags.Compose());
    }
    /// Allows setting additional Dirty Bits based on the ones already set.
    ///
    /// @param bits The current Dirty Bits.
    /// @return The new set of Dirty Bits which replace the original one.
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override { return bits & HdChangeTracker::AllDirty; }
    /// Initialize a given representation for the rprim.
    ///
    /// Currently unused.
    ///
    /// @param reprName Name of the representation to initialize.
    /// @param dirtyBits In/Out HdDirtyBits value, that allows the _InitRepr
    ///  function to set additional Dirty Bits if required for a given
    ///  representation.
    void _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override
    {
        TF_UNUSED(reprToken);
        TF_UNUSED(dirtyBits);
    }

    void SetDeformKeys(int keys) { _deformKeys = keys >= 1 ? keys : 2; }

    int GetDeformKeys() const { return _deformKeys; }


protected:
    /// Geometry deduplication (see HdArnoldRenderDelegate::DeduplicateGeometry), shared by the
    /// mesh and curves rprims. A duplicate renders the node of the rprim owning its geometry
    /// (the canonical) instead of building its own: as a ginstance of it when not instanced,
    /// or through its instancer when it is a point-instancer prototype.

    /// Dirty bits to re-apply when the dedup replaces or rewires the Arnold node, which keeps
    /// none of the previous node's state. The geometry blocks are skipped for a duplicate.
    static constexpr HdDirtyBits _rebuiltNodeDirtyBits =
        HdChangeTracker::AllSceneDirtyBits &
        ~(HdChangeTracker::InitRepr | HdChangeTracker::Varying | HdChangeTracker::DirtyRepr);

    /// Dirty bits that can change the geometry hash, and so re-run the dedup evaluation. Render
    /// tags are hashed too, but they are not a Sync dirty bit anymore from USD 24.08.
    static constexpr HdDirtyBits _geometryHashDirtyBits =
        HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyPrimvar |
        HdChangeTracker::DirtyDisplayStyle | HdChangeTracker::DirtySubdivTags |
        HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtyTransform |
        HdChangeTracker::DirtyCategories | HdChangeTracker::DirtyRenderTag |
        HdChangeTracker::DirtyDoubleSided;

    /// Returns true if this rprim renders the geometry of another rprim.
    bool _IsDuplicate() const { return _shape.GetSharedGeometry() != nullptr; }

    /// Evaluates the geometry deduplication of this rprim, and makes it a canonical, a duplicate
    /// or a plain geometry accordingly. When the Arnold node is replaced or rewired, the dirty
    /// bits and primvars are all dirtied again.
    ///
    /// @param shapeType Arnold node type of the geometry.
    /// @param primvars Primvars of the rprim, all applied to the geometry node.
    /// @param points Points sampled over the shutter; sampled here if empty.
    /// @param displaceable Whether the geometry node carries a displacement shader.
    /// @param hashTopology Callable `bool(uint64_t& hash)` setting @p hash to the hash of the
    ///  type-specific geometry, or returning false if it cannot be deduplicated.
    template <typename HashTopologyFn>
    void _SyncGeometryDedup(
        HdSceneDelegate* sceneDelegate, const SdfPath& id, const AtString& shapeType, HdArnoldPrimvarMap& primvars,
        HdArnoldSampledPrimvarType& points, bool displaceable, HdDirtyBits* dirtyBits, bool& dirtyPrimvars,
        HdArnoldRenderParamInterrupt& param, HashTopologyFn&& hashTopology)
    {
        if (!_renderDelegate->DeduplicateGeometry() ||
            (!dirtyPrimvars && (*dirtyBits & _geometryHashDirtyBits) == 0))
            return;
        // The instancer id is only set by _UpdateInstancer, which SyncShape calls later with the
        // actual dirty bits.
        HdDirtyBits instancerDirtyBits = *dirtyBits;
        HydraType::_UpdateInstancer(sceneDelegate, &instancerDirtyBits);
        const bool instanced = !HydraType::GetInstancerId().IsEmpty();
        // By default only point-instancer prototypes are deduplicated (UsdImaging makes a copy of
        // each prototype per instancer), so that the other geometries don't pay for hashing.
        bool eligible =
            (instanced || _renderDelegate->GetGeometryDedupMode() == HdArnoldRenderDelegate::GeometryDedupMode::All) &&
            primvars.count(HdTokens->points) == 0 && primvars.count(HdTokens->velocities) == 0 &&
            primvars.count(HdTokens->accelerations) == 0;
        if (eligible && points.count == 0) {
            auto* renderParam = reinterpret_cast<HdArnoldRenderParam*>(_renderDelegate->GetRenderParam());
            SamplePrimvar(sceneDelegate, id, HdTokens->points, renderParam->GetShutterRange(), &points);
        }
        eligible = eligible && points.count > 0 && points.values.size() >= points.count;
        for (size_t i = 0; eligible && i < points.count; ++i)
            eligible = points.values[i].IsHolding<VtVec3fArray>();
        uint64_t hash = 0;
        eligible = eligible && hashTopology(hash) &&
                   _HashGeometryState(hash, sceneDelegate, id, points, primvars, instanced, displaceable);
        if (_ApplyGeometryDedup(id, shapeType, eligible, instanced, hash, param)) {
            *dirtyBits |= _rebuiltNodeDirtyBits;
            for (auto& primvar : primvars)
                primvar.second.dirtied = true;
            dirtyPrimvars = true;
        }
    }

    /// Applies the constant primvars to the ginstance of a duplicate: unlike its geometry, its
    /// node-level state (visibility, sidedness, matte, user data) is its own.
    ///
    /// @param skip Callable `bool(const TfToken& name)` returning true for primvars to ignore.
    template <typename SkipFn>
    void _SyncInstanceNodePrimvars(HdArnoldPrimvarMap& primvars, HdArnoldRenderParamInterrupt& param, SkipFn&& skip)
    {
        param.Interrupt();
        _visibilityFlags.ClearPrimvarFlags();
        _sidednessFlags.ClearPrimvarFlags();
        for (auto& primvar : primvars) {
            const auto& desc = primvar.second;
            if (desc.interpolation == HdInterpolationConstant && !skip(primvar.first)) {
                HdArnoldSetConstantPrimvar(
                    GetArnoldNode(), primvar.first, desc.role, desc.value, &_visibilityFlags, &_sidednessFlags,
                    nullptr, _renderDelegate);
            }
        }
        UpdateVisibilityAndSidedness();
    }

    /// Leaves the dedup registry when this rprim is destroyed. Must be called from the derived
    /// destructor before anything else touches the Arnold node.
    void _ReleaseGeometryDedup()
    {
        if (_dedupHash == 0)
            return;
        if (_IsDuplicate()) {
            // Our nodes go first, releasing can destroy the canonical node they reference.
            _shape.DestroyNodes();
            _renderDelegate->ReleaseCanonicalGeometry(_dedupHash);
        } else if (_renderDelegate->LeaveCanonicalGeometry(_dedupHash, GetArnoldNode(), HydraType::GetId())) {
            _shape.ReleaseShapeOwnership();
        }
    }

private:
    /// Folds into @p hash everything but the type-specific topology that ends up on the geometry
    /// node, or returns false if the geometry cannot be hashed reliably:
    ///  - the points at every motion key, and all the primvars (combined commutatively, as
    ///    their map order is not deterministic);
    ///  - the render tag and the light-linking categories of the prim and its instancer;
    ///  - the displacement shader, which a ginstance cannot override;
    ///  - for a point-instancer prototype, the transform, surface shader and sidedness, as its
    ///    instancer renders the canonical node itself. Its prim id and cryptomatte name are
    ///    overridden per instance instead (see HdArnoldShape::_SyncInstances).
    bool _HashGeometryState(
        uint64_t& hash, HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdArnoldSampledPrimvarType& points,
        const HdArnoldPrimvarMap& primvars, bool instanced, bool displaceable) const
    {
        hash = TfHash::Combine(hash, points.count);
        for (size_t i = 0; i < points.count; ++i) {
            if (!points.values[i].CanHash())
                return false;
            hash = TfHash::Combine(hash, points.values[i].GetHash());
            if (i < points.times.size())
                hash = TfHash::Combine(hash, points.times[i]);
        }
        size_t primvarsHash = 0;
        for (const auto& primvar : primvars) {
            const auto& desc = primvar.second;
            if (!desc.value.CanHash())
                return false;
            // The role changes how the value is converted.
            size_t primvarHash = TfHash::Combine(
                primvar.first, desc.role, static_cast<int>(desc.interpolation), desc.value.GetHash());
            if (!desc.valueIndices.empty())
                primvarHash = TfHash::Combine(primvarHash, desc.valueIndices);
            primvarsHash += primvarHash;
        }
        hash = TfHash::Combine(hash, primvarsHash, sceneDelegate->GetRenderTag(id));
        for (const TfToken& category : sceneDelegate->GetCategories(id))
            hash = TfHash::Combine(hash, category);
        if (instanced) {
            for (const TfToken& category : sceneDelegate->GetCategories(HydraType::GetInstancerId()))
                hash = TfHash::Combine(hash, category);
            hash = TfHash::Combine(hash, sceneDelegate->GetTransform(id), sceneDelegate->GetDoubleSided(id));
        }
        if (instanced || displaceable) {
            auto* material = HdArnoldNodeGraph::GetNodeGraph(
                sceneDelegate->GetRenderIndex(), sceneDelegate->GetMaterialId(id), _renderDelegate);
            if (material != nullptr) {
                const auto coordSysBinding = HdArnoldGetCoordSysBinding(sceneDelegate, id);
                if (displaceable)
                    hash = TfHash::Combine(
                        hash, reinterpret_cast<uintptr_t>(material->GetCachedDisplacementShader(coordSysBinding)));
                if (instanced)
                    hash = TfHash::Combine(
                        hash, reinterpret_cast<uintptr_t>(material->GetCachedSurfaceShader(coordSysBinding)));
            }
        }
        return true;
    }

    /// Registers this rprim as the canonical or a duplicate of the geometry identified by
    /// @p hash, or makes it a plain geometry again if not @p eligible. Returns true if the
    /// Arnold node was replaced or rewired. Nothing is touched, and the render is not
    /// interrupted, when the dedup state does not change.
    bool _ApplyGeometryDedup(
        const SdfPath& id, const AtString& shapeType, bool eligible, bool instanced, uint64_t hash,
        HdArnoldRenderParamInterrupt& param)
    {
        _shape.SetForceInstancerNode(eligible && instanced);
        if (!eligible)
            return _LeaveGeometryDedup(id, shapeType, param);
        // Keep meshes and curves apart in the registry, and 0 for "not registered".
        uint64_t typedHash = TfHash::Combine(hash, reinterpret_cast<uintptr_t>(shapeType.c_str()));
        typedHash += typedHash == 0;
        AtNode* const canonical = _shape.GetSharedGeometry();
        if (typedHash == _dedupHash) {
            // Still the canonical, or a duplicate of the same node. Only the way a duplicate
            // instances it can change, when the prim becomes instanced or not.
            if (canonical == nullptr || instanced != _shape.IsInstanceNode())
                return false;
            param.Interrupt();
            _InstanceSharedGeometry(id, shapeType, canonical, instanced);
            return true;
        }
        param.Interrupt();
        bool rebuilt = false;
        // A duplicate stays registered to its previous geometry until it is rewired below, as
        // releasing it can destroy the node it references.
        if (_dedupHash != 0 && canonical == nullptr)
            rebuilt = _LeaveCanonicalGeometry(id, shapeType);
        AtNode* shared = _renderDelegate->AcquireCanonicalGeometry(typedHash, canonical ? nullptr : GetArnoldNode());
        if (shared == nullptr && canonical != nullptr) {
            // Nobody owns this geometry: become its canonical, which requires a real node.
            _RestoreRealGeometryNode(id, shapeType);
            shared = _renderDelegate->AcquireCanonicalGeometry(typedHash, GetArnoldNode());
            rebuilt = true;
        }
        if (shared != nullptr) {
            _InstanceSharedGeometry(id, shapeType, shared, instanced);
            rebuilt = true;
        }
        if (canonical != nullptr)
            _renderDelegate->ReleaseCanonicalGeometry(_dedupHash);
        _dedupHash = typedHash;
        return rebuilt;
    }

    /// Leaves the dedup registry and makes this rprim a plain geometry. Returns true if the
    /// Arnold node was replaced or rewired.
    bool _LeaveGeometryDedup(const SdfPath& id, const AtString& shapeType, HdArnoldRenderParamInterrupt& param)
    {
        if (_dedupHash == 0)
            return false;
        param.Interrupt();
        bool rebuilt = true;
        if (_IsDuplicate()) {
            // Unwire first, releasing can destroy the canonical node.
            _RestoreRealGeometryNode(id, shapeType);
            _renderDelegate->ReleaseCanonicalGeometry(_dedupHash);
        } else {
            rebuilt = _LeaveCanonicalGeometry(id, shapeType);
        }
        _dedupHash = 0;
        return rebuilt;
    }

    /// Releases the canonical registration of this rprim. If duplicates still reference its
    /// node, the render delegate keeps it and this rprim gets a new, empty node: returns true.
    bool _LeaveCanonicalGeometry(const SdfPath& id, const AtString& shapeType)
    {
        if (!_renderDelegate->LeaveCanonicalGeometry(_dedupHash, GetArnoldNode(), id))
            return false;
        _shape.ReleaseShapeOwnership();
        _shape.SetShapeType(shapeType, id, HydraType::GetPrimId());
        return true;
    }

    /// Makes this rprim render the canonical node @p shared.
    void _InstanceSharedGeometry(const SdfPath& id, const AtString& shapeType, AtNode* shared, bool instanced)
    {
        if (instanced) {
            // The instancer references the shared node, this rprim keeps an empty node of its own.
            if (_shape.IsInstanceNode())
                _shape.SetShapeType(shapeType, id, HydraType::GetPrimId());
            _shape.SetPrototypeOverride(shared);
        } else {
            _shape.ConvertToInstanceOf(shared, id, HydraType::GetPrimId());
        }
    }

    /// Stops rendering a shared geometry, leaving this rprim with an empty node of its own.
    void _RestoreRealGeometryNode(const SdfPath& id, const AtString& shapeType)
    {
        if (_shape.IsInstanceNode())
            _shape.SetShapeType(shapeType, id, HydraType::GetPrimId());
        else
            _shape.SetPrototypeOverride(nullptr);
    }

protected:
    /// Returns true if step size is bigger than zero, false otherwise.
    ///
    /// @return True if prim is a volume boundary.
    HDARNOLD_API
    bool _IsVolume() const { return AiNodeGetFlt(GetArnoldNode(), str::step_size) > 0.0f; }

    HdArnoldShape _shape;                                     ///< HdArnoldShape to handle instances and shape creation.
    HdArnoldRenderDelegate* _renderDelegate;                  ///< Pointer to the Arnold Render Delegate.
    HdArnoldRayFlags _visibilityFlags{AI_RAY_ALL};            ///< Visibility of the shape.
    HdArnoldRayFlags _sidednessFlags{AI_RAY_SUBSURFACE};      ///< Sidedness of the shape.
    HdArnoldRayFlags _autobumpVisibilityFlags{AI_RAY_CAMERA}; ///< Autobump visibility of the shape.
    int _deformKeys = 2;                                      ///< Number of deform keys. Used with velocity and accelerations
    bool _skipped = false;
    uint64_t _dedupHash = 0;                                  ///< Geometry hash registered for deduplication, 0 if none.
};

PXR_NAMESPACE_CLOSE_SCOPE
