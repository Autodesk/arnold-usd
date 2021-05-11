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
/// @file scene_delegate/polymesh_adapter.h
///
/// Adapter for converting Arnold polymesh to Hydra mesh.
#pragma once
#include "api.h"

#include <pxr/pxr.h>

#include "rprim_adapter.h"

PXR_NAMESPACE_OPEN_SCOPE

/// @class ImagingArnoldPerspCameraAdapter
///
/// Adapter for polymesh.
class ImagingArnoldPolymeshAdapter : public ImagingArnoldRprimAdapter {
public:
    using BaseAdapter = ImagingArnoldRprimAdapter;

    /// Tells if the polymesh adapter can work with a given Arnold scene delegate.
    ///
    /// @param proxy Pointer to the ImagingArnoldDelegateProxy.
    /// @return True if the adapter works with the Arnold scene delegate, false otherwise.
    IMAGINGARNOLD_API
    bool IsSupported(ImagingArnoldDelegateProxy* proxy) const override;

    /// Populates a given Arnold scene delegate with the Hydra primitive required by the polymesh adapter.
    ///
    /// @param node Pointer to the Arnold node.
    /// @param proxy Pointer to the ImagingArnoldDelegateProxy.
    /// @param id Path of the Hydra primitive.
    IMAGINGARNOLD_API
    void Populate(AtNode* node, ImagingArnoldDelegateProxy* proxy, const SdfPath& id) override;

    /// Gets the mesh topology of an Arnold polymesh.
    ///
    /// @param node Pointer to the Arnold polymesh node.
    /// @return Topology of a Hydra mesh.
    IMAGINGARNOLD_API
    HdMeshTopology GetMeshTopology(const AtNode* node) const override;

    /// Gets the primvar descriptors of an Arnold polymesh.
    ///
    /// @param node Pointer to the Arnold polymesh.
    /// @param interpolation Interpolation of the primvar descriptors to query.
    /// @return Primvar descriptors of a given interpolation type, empty vector if none available.
    IMAGINGARNOLD_API
    HdPrimvarDescriptorVector GetPrimvarDescriptors(const AtNode* node, HdInterpolation interpolation) const override;

    /// Gets a named value from an Arnold polymesh.
    ///
    /// @param node Pointer to the Arnold polymesh.
    /// @param key Name of the value.
    /// @return Value of a given name named value, empty VtValue if not available.
    IMAGINGARNOLD_API
    VtValue Get(const AtNode* node, const TfToken& key) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE
