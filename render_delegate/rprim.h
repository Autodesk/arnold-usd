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

#if PXR_VERSION >= 2102
#include <pxr/imaging/hd/instancer.h>
#endif
#include <pxr/imaging/hd/rprim.h>

#include "render_delegate.h"
#include "shape.h"

PXR_NAMESPACE_OPEN_SCOPE

template <typename HydraType>
class HdArnoldRprim : public HydraType {
public:
#if PXR_VERSION >= 2102
    /// Constructor for HdArnoldRprim.
    ///
    /// @param shapeType AtString storing the type of the Arnold Shape node.
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the primitive.
    HDARNOLD_API
    HdArnoldRprim(const AtString& shapeType, HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
        : HydraType(id), _renderDelegate(renderDelegate), _shape(shapeType, renderDelegate, id, HydraType::GetPrimId())
    {
    }
#else
    /// Constructor for HdArnoldRprim.
    ///
    /// @param shapeType AtString storing the type of the Arnold Shape node.
    /// @param renderDelegate Pointer to the Render Delegate.
    /// @param id Path to the primitive.
    /// @param instancerId Path to the point instancer.
    HDARNOLD_API
    HdArnoldRprim(
        const AtString& shapeType, HdArnoldRenderDelegate* renderDelegate, const SdfPath& id,
        const SdfPath& instancerId)
        : HydraType(id, instancerId),
          _renderDelegate(renderDelegate),
          _shape(shapeType, renderDelegate, id, HydraType::GetPrimId())
    {
    }
#endif

    /// Destructor for HdArnoldRprim.
    ///
    /// Frees the shape and all the ginstances created.
    virtual ~HdArnoldRprim() = default;
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
    /// Syncs internal data and arnold state with hydra.
    HDARNOLD_API
    void SyncShape(
        HdDirtyBits dirtyBits, HdSceneDelegate* sceneDelegate, HdArnoldRenderParamInterrupt& param, bool force = false)
    {
#if PXR_VERSION >= 2102
        // Newer USD versions need to update the instancer before accessing the instancer id.
        HydraType::_UpdateInstancer(sceneDelegate, &dirtyBits);
        // We also force syncing of the parent instancers.
        HdInstancer::_SyncInstancerAndParents(sceneDelegate->GetRenderIndex(), HydraType::GetInstancerId());
#endif
        _shape.Sync(this, dirtyBits, _renderDelegate, sceneDelegate, param, force);
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

    bool SetDeformKeys(int keys)
    {
        const auto k = static_cast<decltype(_deformKeys)>(std::max(0, keys));
        if (_deformKeys != k) {
            _deformKeys = k;
            return true;
        }
        return false;
    }

    uint8_t GetDeformKeys() const { return _deformKeys; }

protected:
    HdArnoldShape _shape;                                ///< HdArnoldShape to handle instances and shape creation.
    HdArnoldRenderDelegate* _renderDelegate;             ///< Pointer to the Arnold Render Delegate.
    uint8_t _deformKeys = HD_ARNOLD_MAX_PRIMVAR_SAMPLES; ///< Number of deform keys.
};

PXR_NAMESPACE_CLOSE_SCOPE
