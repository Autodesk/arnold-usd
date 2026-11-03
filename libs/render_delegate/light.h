//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2019 Luma Pictures
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
//
// Modifications Copyright 2022 Autodesk, Inc.
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
/// @file light.h
///
/// Utilities to translating Hydra lights for the Render Delegate.
#pragma once
#include <pxr/pxr.h>
#include "api.h"

#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/light.h>

#include "hdarnold.h"
#include "render_delegate.h"

#include <functional>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace HdArnoldLight {

/// Returns an instance of HdArnoldLight for handling point lights.
///
/// @param renderDelegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreatePointLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling distant lights.
///
/// @param renderDelegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateDistantLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling disk lights.
///
/// @param renderDelegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateDiskLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling rect lights.
///
/// @param renderDelegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateRectLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling cylinder lights.
///
/// @param renderDelegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateCylinderLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling dome lights.
///
/// @param renderDelegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateDomeLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

HDARNOLD_API
HdLight* CreateGeometryLight(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id);

/// Returns the Arnold light node for any HdLight.
///
/// @param light Pointer to the HdLight.
/// @return Pointer to the Arnold Light, can be nullptr.
AtNode* GetLightNode(const HdLight* light);

/// Computes the light shaders of a light, connected through a node graph (e.g. primvars:arnold:shaders).
///
/// The color terminal is linked to the light color. The filters terminals are returned in
/// nodeGraphFilters if it is not null, otherwise they are set on the light filters.
///
/// @return Path of the node graph, can be empty.
SdfPath ComputeLightShaders(HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate *renderDelegate, const SdfPath &id, const TfToken &attr, AtNode *light,
    std::vector<AtNode*> *nodeGraphFilters = nullptr);

/// Returns the light filter nodes of the UsdLux light:filters relationship of a light, exposed by Hydra 2
/// as the "filters" entry of the light. The paths of the targets are appended to filterPaths if not null.
std::vector<AtNode*> ComputeUsdLuxLightFilters(HdSceneDelegate* sceneDelegate, HdArnoldRenderDelegate *renderDelegate,
    const SdfPath &id, SdfPathVector *filterPaths = nullptr);

/// Returns the names of the given light filter nodes.
VtArray<std::string> GetLightFilterNames(const std::vector<AtNode*> &filters);

/// Sets the light filters, given by node names. The connection is deferred to ProcessConnection,
/// like in the procedural. Nothing is done if the list is empty.
void SetLightFilters(AtNode *light, const VtArray<std::string> &filterNames, HdArnoldRenderDelegate *renderDelegate);

} // namespace HdArnoldLight

PXR_NAMESPACE_CLOSE_SCOPE
