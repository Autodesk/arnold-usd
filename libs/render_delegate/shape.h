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
    /// The node is recreated if it is not of @p shapeType, or if it is a ginstance: those
    /// cannot be reused once initialized.
    ///
    /// @param shapeType New node entry for this Arnold shape node
    /// @param id Path to the primitive.
    /// @param primId Prim ID of the owning rprim, applied to a recreated node.
    void SetShapeType(const AtString& shapeType, const SdfPath& id, int32_t primId);

    /// Turns this shape into a ginstance of @p proto (geometry deduplication). The instance
    /// keeps its own transform and surface shader, and shares the geometry of @p proto.
    ///
    /// @param proto The canonical Arnold node to instance.
    /// @param id Path to the primitive.
    /// @param primId Prim ID of the owning rprim.
    void ConvertToInstanceOf(AtNode* proto, const SdfPath& id, int32_t primId);

    /// Makes this shape's Arnold instancer reference @p proto instead of this shape's own node
    /// (geometry deduplication of point-instancer prototypes), or its own node again if null.
    ///
    /// @param proto The canonical geometry node to instance, or nullptr.
    void SetPrototypeOverride(AtNode* proto) { _sharedGeometry = proto; }

    /// Returns the canonical node this shape renders instead of its own geometry, either as
    /// a ginstance (ConvertToInstanceOf) or through its instancer (SetPrototypeOverride).
    ///
    /// @return The shared geometry node, or nullptr.
    AtNode* GetSharedGeometry() const { return _sharedGeometry; }

    /// Returns true if the Arnold node is a ginstance (see ConvertToInstanceOf).
    ///
    /// @return True if the Arnold node is a ginstance.
    bool IsInstanceNode() const { return _isInstance; }

    /// Forgets the Arnold node without destroying it, when its ownership was transferred.
    void ReleaseShapeOwnership();

    /// Destroys the Arnold node and instancers now, rather than with this shape.
    HDARNOLD_API
    void DestroyNodes();

    /// Hides or shows everything this shape renders through (see HdArnoldRprim::SkipHiddenPrim):
    /// the instancers if any, otherwise the shape node itself. A node shared with other rprims
    /// (a geometry deduplication canonical) must not be disabled, as its instances would
    /// disappear too: its visibility is cleared instead, which its instances do not inherit.
    ///
    /// @param hidden Whether the shape should stop rendering.
    /// @param nodeIsShared True when other rprims may instance this shape's node.
    HDARNOLD_API
    void SetHidden(bool hidden, bool nodeIsShared);

    /// Returns true if this shape is currently not rendering, either because SetHidden hid it
    /// or because the node it renders through was disabled for its render tag.
    ///
    /// @return True if the shape is hidden.
    HDARNOLD_API
    bool IsHidden() const;

    /// Forces the instances to be built through an Arnold instancer node rather than shape
    /// instancing, which bakes the instance matrices onto the node and would prevent it from
    /// being shared (geometry deduplication).
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
    /// @param primId Prim ID of the primitive.
    /// @param cryptoObject Cryptomatte object name of the primitive, if overridden.
    HDARNOLD_API
    void _SyncInstances(
        HdDirtyBits dirtyBits, HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate,
        HdArnoldRenderParamInterrupt& param, const SdfPath& id, const SdfPath& instancerId, bool force,
        int32_t primId, const SdfPath& cryptoObject);
    /// Checks if existing instance visibility for the first @param count instances.
    ///
    /// @param param Reference to HdArnoldRenderParamInterrupt.
    HDARNOLD_API
    void _UpdateInstanceVisibility(HdArnoldRenderParamInterrupt& param);

    HdArnoldRenderDelegate* _renderDelegate; ///< Pointer to the Arnold render delegate.
    std::vector<AtNode*> _instancers;        ///< Pointer to the Arnold instancer and its parent instancers if any.
    AtNode* _shape = nullptr;                ///< Pointer to the Arnold Shape.
    AtNode* _sharedGeometry = nullptr;       ///< Canonical node rendered instead of _shape (see GetSharedGeometry).
    /// How SetHidden hid this shape, so that showing it again undoes exactly that.
    enum class HiddenBy : uint8_t {
        None,       ///< Not hidden by SetHidden (render tags may still have disabled nodes).
        Instancers, ///< The instancer chain was disabled.
        Visibility, ///< The shape's own node is shared: its visibility was cleared.
        Disabled    ///< The shape's own node was disabled.
    };
    HiddenBy _hiddenBy = HiddenBy::None;
    uint8_t _visibility = AI_RAY_ALL;        ///< Visibility of the mesh.
    bool _forceInstancerNode = false;        ///< Force the instancer-node path (see SetForceInstancerNode).
    /// True when _shape was created as a ginstance: AiNodeIs cannot tell once it is initialized.
    bool _isInstance = false;
};

PXR_NAMESPACE_CLOSE_SCOPE
