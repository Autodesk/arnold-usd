// Copyright 2019 Autodesk, Inc.
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

PXR_NAMESPACE_OPEN_SCOPE

class HdRprim;

/// Utility class for handling instanceable Arnold Shapes.
class HdArnoldShape {
public:
    /// Constructor for HdArnoldShape.
    ///
    /// @param shapeType AtString storing the type of the Arnold Shape node.
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the primitive.
    HDARNOLD_API
    HdArnoldShape(const AtString& shapeType, HdArnoldRenderDelegate* delegate, const SdfPath& id, const int32_t primId);

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
    /// Gets the Render Delegate.
    ///
    /// @return Pointer to the Render Delegate.
    HdArnoldRenderDelegate* GetDelegate() { return _delegate; }
    /// Syncs internal data and arnold state with hydra.
    HDARNOLD_API
    void Sync(
        HdRprim* rprim, HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param,
        bool force = false);

    /// Sets the internal visibility parameter.
    ///
    /// @param visibility New value for visibility.
    HDARNOLD_API
    void SetVisibility(uint8_t visibility);

    /// Gets the internal visibility parameter.
    ///
    /// @return Visibility of the shape.
    uint8_t GetVisibility() { return _visibility; }

protected:
    /// Sets a new hydra-provided primId.
    ///
    /// @param primId The new prim ID to set.
    void _SetPrimId(int32_t primId);
    /// Syncs the Instances.
    ///
    /// Creates and updates all the instances and destroys the ones not required
    /// anymore using the Dirty Bits.
    ///
    /// @param dirtyBits Dirty Bits to sync.
    /// @param sceneDelegate Pointer to the Scene Delegate.
    /// @param id Path to the primitive.
    /// @param instancerId Path to the Point Instancer.
    /// @param force Forces updating of the instances even if they are not dirtied.
    void _SyncInstances(
        HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param, const SdfPath& id,
        const SdfPath& instancerId, bool force);
    /// Checks if existing instance visibility for the first @param count instances.
    ///
    /// @param count Number of instance visibilities to update.
    /// @param param HdArnoldRenderParam to stop rendering if it's not nullptr.
    void _UpdateInstanceVisibility(size_t count, HdArnoldRenderParam* param = nullptr);

#ifdef HDARNOLD_USE_INSTANCER
    AtNode* _instancer = nullptr; ///< Pointer to the Arnold Instancer.
#else
    std::vector<AtNode*> _instances; ///< Storing Pointers to the ginstances.
#endif
    AtNode* _shape;                    ///< Pointer to the Arnold Shape.
    HdArnoldRenderDelegate* _delegate; ///< Pointer to the Render Delegate.
    uint8_t _visibility = AI_RAY_ALL;  ///< Visibility of the mesh.
};

PXR_NAMESPACE_CLOSE_SCOPE
