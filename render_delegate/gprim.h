// Copyright 2021 Autodesk, Inc.
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
/// @file gprim.h
///
/// Utilities for handling common gprim behavior.
#pragma once

#include "api.h"

#include <ai.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/rprim.h>

#include "render_delegate.h"
#include "shape.h"

PXR_NAMESPACE_OPEN_SCOPE

template <typename HydraType>
class HdArnoldGprim : public HydraType {
public:
    /// Constructor for HdArnoldShape.
    ///
    /// @param shapeType AtString storing the type of the Arnold Shape node.
    /// @param delegate Pointer to the Render Delegate.
    /// @param id Path to the primitive.
    /// @param instancerId Path to the point instancer.
    HDARNOLD_API
    HdArnoldGprim(
        const AtString& shapeType, HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId)
        : HydraType(id, instancerId), _delegate(delegate), _shape(shapeType, delegate, id, HydraType::GetPrimId())
    {
    }

    /// Destructor for HdArnoldShape.
    ///
    /// Frees the shape and all the ginstances created.
    virtual ~HdArnoldGprim() = default;

    /// Gets the Arnold Shape.
    ///
    /// @return Pointer to the Arnold Shape.
    AtNode* GetShape() { return _shape.GetShape(); }
    /// Gets the Arnold Shape.
    ///
    /// @return Constant pointer to the Arnold Shape.
    const AtNode* GetShape() const { return _shape.GetShape(); }
    /// Gets the Render Delegate.
    ///
    /// @return Pointer to the Render Delegate.
    HdArnoldRenderDelegate* GetDelegate() { return _delegate; }
    /// Syncs internal data and arnold state with hydra.
    HDARNOLD_API
    void SyncShape(
        HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param, bool force = false)
    {
        _shape.Sync(this, dirtyBits, _delegate, sceneDelegate, param, force);
    }
    /// Sets the internal visibility parameter.
    ///
    /// @param visibility New value for visibility.
    HDARNOLD_API
    void SetShapeVisibility(uint8_t visibility) { _shape.SetVisibility(visibility); }

    /// Gets the internal visibility parameter.
    ///
    /// @return Visibility of the shape.
    uint8_t GetShapeVisibility() const { return _shape.GetVisibility(); }

protected:
    HdArnoldShape _shape;              ///< HdArnoldShape to handle instances and shape creation.
    HdArnoldRenderDelegate* _delegate; ///< Pointer to the Arnold Render Delegate.
};

PXR_NAMESPACE_CLOSE_SCOPE