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
        AtNode* node = GetArnoldNode();
        if (node == nullptr)
            return skip;

        bool wasDisabled = AiNodeIsDisabled(node);
        if (skip == wasDisabled)
            return skip;

        if (wasDisabled) {
            // We're about to turn this disabled node into an active one.
            // But we must ensure it hadn't been disabled due to its render tags.
            // If so, we don't want to stop the render nor change its state
            if (!_renderDelegate->IsVisibleRenderTag(sceneDelegate->GetRenderTag(id)))
                return false;
        }
        param.Interrupt();
        AiNodeSetDisabled(GetArnoldNode(), skip);
    
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
    /// Geometry deduplication (see HdArnoldRenderDelegate::DeduplicateGeometry). The state and
    /// wiring below are shared by every geometry rprim (mesh, curves): the type-specific Sync
    /// only has to decide eligibility and compute a geometry hash, then call _ApplyGeometryDedup.

    /// Hands this rprim's Arnold node over to the render delegate if it is a dedup canonical
    /// still referenced by instances (so the node outlives this rprim); otherwise the delegate
    /// just cleans up its registry entry and the node is destroyed normally. Call this early
    /// from the derived destructor, before any code that resets shared arrays on the node:
    /// on adoption ReleaseShapeOwnership() clears the shape so GetArnoldNode() becomes null and
    /// that cleanup is (correctly) skipped, keeping the adopted node's geometry intact.
    void _HandOffDedupOnDestroy()
    {
        if (_dedupRegistered && _renderDelegate->OnGeometryDestroyed(HydraType::GetId(), GetArnoldNode()))
            _shape.ReleaseShapeOwnership();
    }

    /// Creates a fresh, real (non-instanced) node of @p realShapeType (str::polymesh /
    /// str::curves) for this rprim, replacing whatever node it currently owns. The new node is
    /// empty, so the caller must force a full geometry rebuild. The caller is also responsible
    /// for interrupting the render first (Arnold nodes are created and destroyed here).
    void _CreateRealGeometryNode(const SdfPath& id, const AtString& realShapeType)
    {
        _shape.SetShapeType(realShapeType, id);
        // Whatever ginstance we had is gone with the old node.
        _ginstancePrototype = nullptr;
        // A freshly created polymesh must reset its subdivision: unlike the one built in
        // HdArnoldMesh's constructor it would otherwise keep arnold's default of 1 iteration.
        // Curves have no subdiv_iterations parameter.
        if (realShapeType == str::polymesh)
            AiNodeSetByte(GetArnoldNode(), str::subdiv_iterations, 0);
    }

    /// Hands this rprim's Arnold node over to the render delegate if it is a dedup canonical
    /// that other rprims instance, and gives this rprim a new, empty node of @p realShapeType
    /// to build into. Returns true if that happened, in which case the caller must force a
    /// full geometry rebuild (see _HandOffDedupOnDestroy for the same operation on destroy).
    ///
    /// Must be called before anything destroys or repurposes the node this rprim owns while it
    /// is registered as a canonical - Arnold instances point at their prototype's geometry
    /// arrays instead of copying them, so destroying a prototype dangles all of them (see
    /// HdArnoldRenderDelegate::HandOffCanonicalGeometry).
    bool _HandOffCanonicalGeometry(const SdfPath& id, const AtString& realShapeType)
    {
        // Only a real geometry node can have been published as a canonical.
        if (_isInstance || !_renderDelegate->HandOffCanonicalGeometry(id, GetArnoldNode()))
            return false;
        // The node belongs to the render delegate now: drop it without destroying it.
        _shape.ReleaseShapeOwnership();
        _CreateRealGeometryNode(id, realShapeType);
        return true;
    }

    /// Turns this rprim's dedup instance state back into a real, non-instanced node of
    /// @p realShapeType (str::polymesh / str::curves). No-op if this rprim is not currently a
    /// dedup instance. The caller is responsible for interrupting the render first (the
    /// ginstance flavor destroys and recreates the Arnold node).
    void _RebuildRealGeometryNode(const SdfPath& id, const AtString& realShapeType)
    {
        if (!_isInstance)
            return;
        if (_sharedPrototype != nullptr) {
            // Instanced-prototype flavor: stop redirecting the instancer to the shared canonical.
            _shape.SetPrototypeOverride(nullptr);
            _sharedPrototype = nullptr;
        } else {
            // Ginstance flavor: recreate a real geometry node in place of the ginstance.
            _CreateRealGeometryNode(id, realShapeType);
        }
        _isInstance = false;
        _canonicalPath = SdfPath();
    }

    /// Reverts any dedup state (registry entry and instance wiring) so this rprim can be
    /// built as a real, non-instanced node of @p realShapeType. Sets @p dirtyBits /
    /// @p dirtyPrimvars when the geometry has to be rebuilt from scratch afterwards. No-op if
    /// this rprim is not registered with the dedup registry.
    void _RevertGeometryDedup(
        const SdfPath& id, const AtString& realShapeType, HdDirtyBits* dirtyBits, bool& dirtyPrimvars,
        HdArnoldRenderParamInterrupt& param)
    {
        if (!_dedupRegistered)
            return;
        // Leaving the registry can destroy an adopted canonical node, and rebuilding the real
        // node destroys/creates this rprim's node; neither may happen while rendering.
        param.Interrupt();
        // If our node is a canonical other rprims instance, it must outlive us: hand it over
        // and continue on a new one (the render delegate destroys it once the last instance
        // is gone). Otherwise we keep it and ReleaseCanonicalGeometry just drops the entry.
        const bool handedOff = _HandOffCanonicalGeometry(id, realShapeType);
        const bool wasInstance = _isInstance;
        _renderDelegate->ReleaseCanonicalGeometry(id);
        _dedupRegistered = false;
        _dedupHash = 0;
        _RebuildRealGeometryNode(id, realShapeType);
        // Whether we handed our node over or were rendering another rprim's geometry, we are
        // now sitting on a node with no geometry at all: rebuild all of it.
        if (handedOff || wasInstance) {
            *dirtyBits |=
                HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyPrimvar;
            dirtyPrimvars = true;
        }
    }

    /// Registers this rprim with the geometry-dedup registry using the caller-computed
    /// eligibility and geometry @p hash, and wires up instancing. Returns true if this rprim
    /// is a dedup instance - the caller must then refresh its local node pointer
    /// (GetArnoldNode()) and skip translating geometry. @p realShapeType is the node type to
    /// restore on revert (str::polymesh / str::curves); it is also folded into the registry key
    /// so a mesh and a curve with a colliding geometry hash are never merged. @p param is used
    /// to interrupt the render before any Arnold node is created, destroyed or re-pointed;
    /// when nothing about this rprim's dedup status changes, no node is touched and the render
    /// is not interrupted.
    bool _ApplyGeometryDedup(
        const SdfPath& id, bool eligible, bool instanced, uint64_t hash, const AtString& realShapeType,
        HdDirtyBits* dirtyBits, bool& dirtyPrimvars, HdArnoldRenderParamInterrupt& param)
    {
        // A prototype that might itself be shared as a canonical must stay a plain, shareable
        // node (shape-instancing would bake instance_matrix onto it); force the arnold
        // instancer-node path for every eligible instanced prototype (see SetForceInstancerNode).
        _shape.SetForceInstancerNode(eligible && instanced);
        if (!eligible) {
            // No longer eligible for dedup: leave the registry (dirtying any duplicates if we
            // were their canonical) and, if we were an instance, force a full geometry rebuild
            // on the freshly recreated node.
            if (_dedupRegistered)
                _RevertGeometryDedup(id, realShapeType, dirtyBits, dirtyPrimvars, param);
            return false;
        }

        // Distinguish node types in the shared registry: AtString interns its storage, so
        // equal shape types share a pointer and different ones never collide.
        const uint64_t typedHash = TfHash::Combine(hash, reinterpret_cast<uintptr_t>(realShapeType.c_str()));
        // When the geometry identity changed, acquiring below releases the old association,
        // which can destroy an adopted canonical node.
        if (_dedupRegistered && _dedupHash != typedHash) {
            param.Interrupt();
            // We are leaving the geometry we were registered against. If we own its canonical
            // node and other rprims instance it, that node cannot follow us: hand it over to
            // the render delegate and continue on a new one. Without this, becoming a
            // duplicate below (ConvertToInstanceOf destroys and recreates this rprim's node)
            // would destroy a node those rprims point at, dangling the geometry arrays their
            // Arnold instances share with it - the crash this dedup path used to hit after a
            // few interactive edits (ARNOLD-17180).
            if (_HandOffCanonicalGeometry(id, realShapeType)) {
                *dirtyBits |=
                    HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyPrimvar;
                dirtyPrimvars = true;
            }
        }
        SdfPath canonicalPath;
        bool pending = false;
        // Only offer our node as a canonical candidate if it is a real geometry node; while we
        // are an instance we have no geometry to share, so if we become the canonical the entry
        // stays pending until we have rebuilt a real node and published it (below).
        AtNode* canonical = _renderDelegate->AcquireCanonicalGeometry(
            id, _isInstance ? nullptr : GetArnoldNode(), typedHash, &canonicalPath, &pending);
        // AcquireCanonicalGeometry always records this rprim in the registry (as the
        // canonical or as a duplicate), so from now on the destructor must call
        // OnGeometryDestroyed to clean up / hand off the node.
        _dedupRegistered = true;
        _dedupHash = typedHash;

        if (canonical != nullptr) {
            // This rprim is a duplicate of an existing canonical. Wire up the instancing, but
            // only touch the Arnold nodes when something actually changed: a duplicate that
            // stays a duplicate of the same canonical is a strict no-op (no node churn, no
            // render interruption) - this is what keeps broadcast edits (e.g. authoring a
            // primvar on every prim at once) cheap and safe.
            if (instanced) {
                if (_isInstance && _sharedPrototype == canonical) {
                    // Same canonical node: pure no-op, just track its (possibly updated) path.
                    _canonicalPath = canonicalPath;
                    return true;
                }
                if (_isInstance && _sharedPrototype == nullptr) {
                    // Flavor switch (ginstance -> instanced prototype): rebuild a real node for
                    // the instancer path to reference alongside the prototype override.
                    param.Interrupt();
                    _RebuildRealGeometryNode(id, realShapeType);
                }
                // Redirect this prototype's instancer to the shared canonical node and skip
                // building this prototype's own geometry (member-only; the instancer rebuild
                // in HdArnoldShape interrupts the render itself before touching nodes).
                _sharedPrototype = canonical;
                _shape.SetPrototypeOverride(canonical);
            } else {
                // Compare against the prototype we recorded rather than querying the node with
                // AiNodeGetPtr(node, str::node): an initialized ginstance has mutated into its
                // prototype's node type and no longer has a "node" parameter (see
                // HdArnoldShape::SetShapeType), so that query always returned null after the
                // first render. This fast path was therefore dead, and every duplicate was
                // destroyed and recreated on every edit.
                if (_isInstance && _sharedPrototype == nullptr && _ginstancePrototype == canonical) {
                    // Already a ginstance of this canonical: pure no-op, just track its
                    // (possibly updated) path.
                    _canonicalPath = canonicalPath;
                    return true;
                }
                if (_sharedPrototype != nullptr) {
                    // Flavor switch (instanced prototype -> ginstance): drop the override; the
                    // node is converted to a ginstance right below.
                    _shape.SetPrototypeOverride(nullptr);
                    _sharedPrototype = nullptr;
                }
                // Turn this rprim into a ginstance of the canonical. This always creates a new
                // Arnold node: an initialized ginstance cannot be re-pointed at another
                // prototype (see HdArnoldShape::SetShapeType), which is why the no-op fast
                // path above matters.
                param.Interrupt();
                _shape.ConvertToInstanceOf(canonical, id);
                _ginstancePrototype = canonical;
            }
            _isInstance = true;
            // The duplicate must re-sync whenever its canonical changes or is removed; the
            // type-specific material assignment registers this dependency from _canonicalPath.
            _canonicalPath = canonicalPath;
            // Make sure the duplicate gets its transform, visibility and shader (re)applied
            // by the type-specific Sync (and, for the instanced case, its instancer rebuilt
            // with the redirected prototype). Force primvars too: a ginstance is a fresh
            // node that still needs its node-level constant primvars re-declared.
            *dirtyBits |= HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility |
                          HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtyPrimvar;
            dirtyPrimvars = true;
            return true;
        }

        if (pending) {
            // Another rprim claimed this geometry as canonical in this same parallel Sync pass
            // but has not published its node yet. Keep our current state - the registry queued
            // us and we will be dirtied (and convert) once the canonical is published; attaching
            // now would hand us a node that is mid-rebuild on another thread.
            return _isInstance;
        }

        // This rprim is (or remains) the canonical for this geometry.
        if (_isInstance) {
            // We were an instance and just became the canonical: rebuild a real geometry node,
            // publish it (un-pending the registry entry and dirtying any queued duplicates),
            // and force a full rebuild of the freshly created, empty node.
            param.Interrupt();
            _RebuildRealGeometryNode(id, realShapeType);
            _renderDelegate->PublishCanonicalGeometry(id, typedHash, GetArnoldNode());
            *dirtyBits |=
                HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyPrimvar;
            dirtyPrimvars = true;
        }
        return false;
    }

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

    // Geometry deduplication state (shared by mesh/curves; see _ApplyGeometryDedup).
    bool _isInstance = false;           ///< True when this rprim is a dedup duplicate (geometry not built), either flavor below.
    bool _dedupRegistered = false;      ///< True while this rprim has an entry in the dedup registry (canonical or duplicate); lets the destructor skip OnGeometryDestroyed for the many rprims that never deduplicate.
    AtNode* _sharedPrototype = nullptr; ///< Canonical node this prototype's instancer references (instanced flavor); null for the ginstance flavor.
    AtNode* _ginstancePrototype = nullptr; ///< Canonical node our ginstance was pointed at (ginstance flavor). Tracked here because an initialized ginstance no longer exposes a "node" parameter to query.
    SdfPath _canonicalPath;             ///< Path of the canonical this one shares (dedup), empty otherwise.
    uint64_t _dedupHash = 0;            ///< Typed geometry hash this rprim is registered under (dedup), 0 otherwise.
};

PXR_NAMESPACE_CLOSE_SCOPE
