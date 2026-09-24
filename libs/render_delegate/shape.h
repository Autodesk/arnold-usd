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
/// @file shape.h
///
/// Utilities for handling instanceable Arnold Shapes.
#pragma once

#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/rprim.h>

#include "render_delegate.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprim;

/// Returns the origin (scene) path of @p id when it is a copy of a point-instancer prototype
/// that UsdImaging re-rooted, and nothing otherwise.
///
/// This is the path the cryptomatte object name of such a prototype is overridden with (see
/// HdArnoldShape::Sync): the re-rooted copies carry a hash suffix that has to be stripped for
/// them to matte together.
///
/// Always returns false without Hydra 2 (the schemas this reads do not exist there), which
/// matches Sync: the override is only written in that mode.
///
/// @param sceneDelegate Pointer to the Hydra Scene Delegate.
/// @param id Path of the primitive.
/// @param originPath Output, the origin path when this returns true, untouched otherwise.
/// @return True if @p id is an instanced prototype with an origin path.
HDARNOLD_API
bool HdArnoldGetPrimOriginPath(HdSceneDelegate* sceneDelegate, const SdfPath& id, SdfPath& originPath);

/// Utility class for handling instanceable Arnold Shapes.
class HdArnoldShape {
public:
    /// Constructor for HdArnoldShape.
    ///
    /// @param shapeType AtString storing the type of the Arnold Shape node.
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the primitive.
    /// @param primId Integer ID of the primitive used for the primID pass.
    HDARNOLD_API
    HdArnoldShape(
        const AtString& shapeType, HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const int32_t primId);

    /// Destructor for HdArnoldShape.
    ///
    /// Frees the shape and all the ginstances created.
    HDARNOLD_API
    ~HdArnoldShape();

    HdArnoldShape(const HdArnoldShape&) = delete;
    HdArnoldShape(HdArnoldShape&&) = delete;

    /// Gets the Arnold Shape.
    ///
    /// @return Pointer to the Arnold Shape.
    AtNode* GetShape() { return _shape; }

    /// Gets the Arnold Shape.
    ///
    /// @return Constant pointer to the Arnold Shape.
    const AtNode* GetShape() const { return _shape; }

    /// Modifies the Arnold Shape for a given primitive.
    /// This can happen e.g. with primitives of type ArnoldProceduralCustom
    /// where the node type is an attribute
    ///
    /// The node is kept when it is already of @p shapeType, and destroyed and recreated
    /// otherwise. A ginstance is always recreated: it cannot be reused once initialized.
    ///
    /// @param shapeType New node entry for this Arnold shape node
    /// @param id Path to the primitive.
    /// @param primId Prim ID of the owning rprim (HdRprim::GetPrimId), re-applied when the
    ///  node has to be recreated.
    void SetShapeType(const AtString& shapeType, const SdfPath& id, int32_t primId);

    /// Turns this shape into an Arnold ginstance referencing @p proto.
    ///
    /// Used by the mesh deduplication: a mesh whose geometry is identical to a
    /// previously seen one is rendered as an instance of that canonical node instead
    /// of duplicating the geometry. The instance keeps its own transform (inherit_xform
    /// is disabled) and can carry its own surface shader, while the geometry (and its
    /// BVH) is shared with @p proto.
    ///
    /// @param proto The canonical Arnold node to instance.
    /// @param id Path to the primitive.
    /// @param primId Prim ID of the owning rprim (HdRprim::GetPrimId).
    void ConvertToInstanceOf(AtNode* proto, const SdfPath& id, int32_t primId);

    /// Relinquishes ownership of the Arnold shape node without destroying it, returning
    /// the node. Used when the render delegate adopts a canonical mesh node that is still
    /// referenced by instances after its owning rprim is destroyed.
    ///
    /// @return The Arnold shape node (now owned by the caller), or nullptr.
    AtNode* ReleaseShapeOwnership();

    /// Overrides the geometry node that this shape's Arnold instancer references.
    ///
    /// Used by the mesh deduplication for instanced prototypes: when a point-instancer
    /// prototype is geometrically identical to a previously seen one, its instancer is
    /// pointed at the shared canonical polymesh instead of this shape's (empty) node,
    /// avoiding duplicated geometry. Pass nullptr to reference this shape's own node.
    ///
    /// @param proto The canonical geometry node to instance, or nullptr.
    void SetPrototypeOverride(AtNode* proto) { _prototypeOverride = proto; }

    /// Returns the shared canonical geometry node this shape's instancer references, or
    /// nullptr when it references this shape's own node (i.e. no dedup override is active).
    /// This is also what identifies the instanced-prototype dedup flavor (see
    /// HdArnoldRprim::_ApplyGeometryDedup).
    ///
    /// @return The canonical geometry node the instancer references, or nullptr.
    AtNode* GetPrototypeOverride() const { return _prototypeOverride; }

    /// Hides or shows everything this shape renders through, for the "skip invisible prim"
    /// path (HdArnoldRprim::SkipHiddenPrim).
    ///
    /// Which node that is depends on how the shape renders, and getting it wrong is silent:
    ///  - with an arnold instancer chain, the instancers are what render, and they are
    ///    disabled. Disabling the source shape instead does not hide the instances an
    ///    instancer draws from it, and once the geometry dedup redirects the leaf instancer to
    ///    a shared canonical (SetPrototypeOverride) the source shape is not even the node being
    ///    instanced. Hiding it that way left an interactively hidden prototype on screen. This
    ///    mostly showed with the geometry dedup, which forces the instancer-node path
    ///    (SetForceInstancerNode) where a simple point instancer would otherwise use
    ///    shape-instancing - a single node, which disabling does hide - but it applied to any
    ///    prim rendered through an arnold instancer (nested instancers,
    ///    HDARNOLD_SHAPE_INSTANCING=0).
    ///  - a shape whose own node is shared as a geometry dedup canonical must not be disabled:
    ///    other rprims render that node, through their instancers or their own arnold
    ///    instance nodes, and would disappear with it. It is hidden by clearing its visibility
    ///    instead, which its instances do not inherit - they always set their own (see
    ///    SetVisibility and HdArnoldRenderDelegate::_DetachAdoptedGeometry).
    ///  - anything else is disabled, which is the cheapest of the three.
    ///
    /// @param hidden Whether the shape should stop rendering.
    /// @param nodeIsShared True when this shape's own node may be instanced by other rprims
    ///  (it is a geometry dedup canonical). Irrelevant when the shape has instancers.
    HDARNOLD_API
    void SetHidden(bool hidden, bool nodeIsShared);

    /// Returns true if this shape is currently not rendering, either because SetHidden hid it
    /// or because the node it renders through was disabled elsewhere (render tags).
    ///
    /// @return True if the shape is hidden.
    HDARNOLD_API
    bool IsHidden() const;

    /// Forces this shape's instances to be built through an Arnold instancer node rather
    /// than shape-instancing (baking instance_matrix onto the polymesh).
    ///
    /// Used by the mesh deduplication for instanced prototypes: a prototype that may be
    /// shared as a canonical geometry must remain a plain polymesh (shape-instancing would
    /// bake instance_matrix onto it and make it unusable as a shared prototype / instance
    /// target). Point-instancer prototypes that participate in dedup therefore always use
    /// the instancer-node path.
    ///
    /// @param force Whether to force the Arnold instancer-node path.
    void SetForceInstancerNode(bool force) { _forceInstancerNode = force; }

    /// Syncs internal data and arnold state with hydra.
    ///
    /// @param rprim Pointer to the Hydra render primitive.
    /// @param dirtyBits Hydra dirty bits to sync.
    /// @param sceneDelegate Pointer to the Hydra scene delegate.
    /// @param param Reference to the HdArnold struct handling interrupt eents.
    /// @param force Whether or not to force recreating instances.
    HDARNOLD_API
    void Sync(
        HdRprim* rprim, HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParamInterrupt& param,
        bool force = false);

    /// @brief Update the render tag of the rprim
    /// @param rprim 
    /// @param delegate 
    /// @param param 
    void UpdateRenderTag(HdRprim* rprim, HdSceneDelegate *delegate, HdArnoldRenderParamInterrupt& param);

    /// Sets the internal visibility parameter.
    ///
    /// @param visibility New value for visibility.
    HDARNOLD_API
    void SetVisibility(uint8_t visibility);

    /// Gets the internal visibility parameter.
    ///
    /// @return Visibility of the shape.
    uint8_t GetVisibility() const { return _visibility; }

    /// Returns the Initial Dirty Bits handled by HdArnoldShape.
    ///
    /// @return The initial dirty bit mask.
    static HdDirtyBits GetInitialDirtyBitsMask()
    {
        return HdChangeTracker::DirtyInstancer | HdChangeTracker::DirtyInstanceIndex |
               HdChangeTracker::DirtyCategories | HdChangeTracker::DirtyPrimID;
    }

protected:
    /// Sets a new hydra-provided primId.
    ///
    /// @param primId The new prim ID to set.
    HDARNOLD_API
    void _SetPrimId(int32_t primId);
    /// Syncs the Instances.
    ///
    /// Creates and updates all the instances and destroys the ones not required
    /// anymore using the Dirty Bits.
    ///
    /// @param dirtyBits Dirty Bits to sync.
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param param Reference to HdArnoldRenderParamInterrupt.
    /// @param id Path to the primitive.
    /// @param instancerId Path to the Point Instancer.
    /// @param force Forces updating of the instances even if they are not dirtied.
    HDARNOLD_API
    void _SyncInstances(
        HdDirtyBits dirtyBits, HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate,
        HdArnoldRenderParamInterrupt& param, const SdfPath& id, const SdfPath& instancerId, bool force);
    /// Checks if existing instance visibility for the first @param count instances.
    ///
    /// @param param Reference to HdArnoldRenderParamInterrupt.
    HDARNOLD_API
    void _UpdateInstanceVisibility(HdArnoldRenderParamInterrupt& param);

    HdArnoldRenderDelegate* _renderDelegate; ///< Pointer to the Arnold render delegate.
    std::vector<AtNode*> _instancers;        ///< Pointer to the Arnold instancer and its parent instancers if any.
    AtNode* _shape = nullptr;                ///< Pointer to the Arnold Shape.
    AtNode* _prototypeOverride = nullptr;    ///< Shared canonical geometry the instancer references (mesh dedup); null = use _shape.
    bool _forceInstancerNode = false;        ///< Force the instancer-node path instead of shape-instancing (mesh dedup).
    /// True when _shape was created as an arnold instance node. Two things it is NOT:
    /// AiNodeIs(_shape, str::ginstance), which turns false as soon as the node is initialized
    /// (see SetShapeType), and HdArnoldRprim::_isInstance, which is also set for a
    /// deduplicated prototype whose instancer references a shared canonical - that one owns a
    /// plain geometry node, not an instance node.
    bool _isInstance = false;
    /// How SetHidden hid this shape, so that showing it again undoes exactly that. Also needed
    /// on top of querying the nodes: a cleared visibility is invisible to AiNodeIsDisabled.
    enum class HiddenBy : uint8_t {
        None,       ///< Not hidden by SetHidden (render tags may still have disabled nodes).
        Instancers, ///< The instancer chain was disabled.
        Visibility, ///< The shape's own node is shared: its visibility was cleared.
        Disabled    ///< The shape's own node was disabled.
    };
    HiddenBy _hiddenBy = HiddenBy::None;
    uint8_t _visibility = AI_RAY_ALL;        ///< Visibility of the mesh.
    /// Hydra prim ID last applied to _shape (_SetPrimId). Kept so the instancer path can
    /// republish it per instance when the geometry dedup makes the instancer reference a
    /// shared canonical node whose own hydra_primId belongs to another rprim.
    int32_t _primId = -1;
    /// Cryptomatte object name last applied to _shape (see HdArnoldGetPrimOriginPath), empty
    /// when this prim is not a re-rooted prototype copy. Republished per instance for the same
    /// reason as _primId.
    SdfPath _cryptoObjectPath;
};

PXR_NAMESPACE_CLOSE_SCOPE
