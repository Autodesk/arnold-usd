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
    /// Frees the shape and its instancers.
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
    /// (the canonical) through Arnold instancers instead of building its own: its point
    /// instancers when it is a point-instancer prototype, otherwise a single-instance instancer.
    /// It keeps an empty, hidden node of its own (see HdArnoldShape::SetPrototypeOverride).

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

    /// Returns true if this rprim renders the geometry of another rprim through a
    /// single-instance instancer (see HdArnoldShape::_SyncSharedGeometryInstancer).
    bool _IsNonInstancedDuplicate() const { return _IsDuplicate() && HydraType::GetInstancerId().IsEmpty(); }

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
        const _DedupChange change = _ApplyGeometryDedup(id, shapeType, eligible, instanced, hash, param);
        if (change == _DedupChange::Node) {
            *dirtyBits |= _rebuiltNodeDirtyBits;
            for (auto& primvar : primvars)
                primvar.second.dirtied = true;
            dirtyPrimvars = true;
        } else if (change == _DedupChange::Instancers) {
            *dirtyBits |= HdChangeTracker::DirtyInstancer;
        } else if (change == _DedupChange::SameHash && !_IsDuplicate()) {
            // The canonical geometry hashes the same, so its points, primvars and topology are
            // the same as what its node already has: don't set them again. Setting them is not
            // free, and it must be avoided: the instancers rendering this node share its arrays,
            // and re-setting some of them makes Arnold reinitialize the node (e.g. the points of
            // a curves node), leaving these instances with freed arrays.
            // The primvars keep their dirtied flags, so they are still applied if the node is
            // rebuilt later.
            *dirtyBits &= ~(HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyPoints |
                            HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyWidths |
                            HdChangeTracker::DirtyNormals);
            dirtyPrimvars = false;
        }
    }

    /// Applies the constant primvars of a non-instanced duplicate to its own node, so that its
    /// visibility and sidedness, which its instance overrides on the canonical, include them.
    ///
    /// @param skip Callable `bool(const TfToken& name)` returning true for primvars to ignore.
    template <typename SkipFn>
    void _SyncDuplicateConstantPrimvars(HdArnoldPrimvarMap& primvars, HdArnoldRenderParamInterrupt& param, SkipFn&& skip)
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
    ///  - the displacement shader, which an instance cannot override;
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

    /// What _ApplyGeometryDedup changed.
    enum class _DedupChange {
        None,       ///< Nothing.
        SameHash,   ///< Nothing, the rprim is still registered with the same geometry.
        Instancers, ///< The Arnold instancers were destroyed, the node is unchanged.
        Node        ///< The Arnold node was replaced or rewired.
    };

    /// Registers this rprim as the canonical or a duplicate of the geometry identified by
    /// @p hash, or makes it a plain geometry again if not @p eligible. Nothing is touched, and
    /// the render is not interrupted, when the dedup state does not change.
    _DedupChange _ApplyGeometryDedup(
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
            // Still the canonical, or a duplicate of the same node. Only the instancers of a
            // duplicate can change, when the prim becomes instanced or not.
            if (canonical == nullptr || instanced == _dedupInstanced)
                return _DedupChange::SameHash;
            param.Interrupt();
            _shape.DestroyInstancers();
            _dedupInstanced = instanced;
            return _DedupChange::Node;
        }
        param.Interrupt();
        // Our Arnold instancers keep referencing the previous canonical node until the
        // instances sync rebuilds them, which happens after this. Leaving or releasing that
        // canonical below can destroy it, and Arnold instances share their prototype's arrays:
        // destroy our instancers first, so that nothing references a destroyed node.
        const bool droppedInstancers = _shape.DestroyInstancers();
        bool rebuilt = false;
        // A duplicate stays registered to its previous geometry until it is rewired below, as
        // releasing it can destroy the node it references.
        if (_dedupHash != 0 && canonical == nullptr)
            rebuilt = _LeaveCanonicalGeometry(id, shapeType);
        AtNode* shared = _renderDelegate->AcquireCanonicalGeometry(typedHash, canonical ? nullptr : GetArnoldNode());
        if (shared == nullptr && canonical != nullptr) {
            // Nobody owns this geometry: become its canonical, with our own node.
            _shape.SetPrototypeOverride(nullptr);
            shared = _renderDelegate->AcquireCanonicalGeometry(typedHash, GetArnoldNode());
            rebuilt = true;
        }
        if (shared != nullptr) {
            _InstanceSharedGeometry(id, shapeType, shared);
            rebuilt = true;
        }
        if (canonical != nullptr)
            _renderDelegate->ReleaseCanonicalGeometry(_dedupHash);
        _dedupHash = typedHash;
        _dedupInstanced = instanced;
        return _ToDedupChange(rebuilt, droppedInstancers);
    }

    /// Leaves the dedup registry and makes this rprim a plain geometry.
    _DedupChange _LeaveGeometryDedup(const SdfPath& id, const AtString& shapeType, HdArnoldRenderParamInterrupt& param)
    {
        if (_dedupHash == 0)
            return _DedupChange::None;
        param.Interrupt();
        // Our instancers can reference the canonical node, see _ApplyGeometryDedup.
        const bool droppedInstancers = _shape.DestroyInstancers();
        bool rebuilt = true;
        if (_IsDuplicate()) {
            // Unwire first, releasing can destroy the canonical node.
            _shape.SetPrototypeOverride(nullptr);
            _renderDelegate->ReleaseCanonicalGeometry(_dedupHash);
        } else {
            rebuilt = _LeaveCanonicalGeometry(id, shapeType);
        }
        _dedupHash = 0;
        return _ToDedupChange(rebuilt, droppedInstancers);
    }

    static _DedupChange _ToDedupChange(bool rebuilt, bool droppedInstancers)
    {
        return rebuilt ? _DedupChange::Node : droppedInstancers ? _DedupChange::Instancers : _DedupChange::None;
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

    /// Makes this rprim render the canonical node @p shared through its instancers, keeping an
    /// empty node of its own. Its node can still hold its previous geometry, e.g. when it was the
    /// canonical of a geometry nobody else shared: replace it so that its arrays are released.
    /// Nothing references it anymore, our instancers were destroyed in _ApplyGeometryDedup. A
    /// node created by the rprim constructor is empty, so it is kept: it can't always be
    /// destroyed while the procedural is expanded, which would leave its name taken.
    void _InstanceSharedGeometry(const SdfPath& id, const AtString& shapeType, AtNode* shared)
    {
        if (_HasOwnGeometry(shapeType))
            _shape.SetShapeType(shapeType, id, HydraType::GetPrimId(), true);
        _shape.SetPrototypeOverride(shared);
    }

    /// Returns true if the Arnold node of this rprim holds a geometry of type @p shapeType.
    bool _HasOwnGeometry(const AtString& shapeType) const
    {
        const AtNode* node = GetArnoldNode();
        if (node == nullptr)
            return false;
        const AtArray* points = AiNodeGetArray(node, shapeType == str::curves ? str::points : str::vlist);
        return points != nullptr && AiArrayGetNumElements(points) > 0;
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
    bool _dedupInstanced = false;                             ///< Whether the prim was instanced when _dedupHash was registered.
};

PXR_NAMESPACE_CLOSE_SCOPE
