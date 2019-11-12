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
// Modifications Copyright 2019 Autodesk, Inc.
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

#include <pxr/imaging/hd/light.h>

#include "hdarnold.h"
#include "render_delegate.h"

#include <functional>

PXR_NAMESPACE_OPEN_SCOPE

namespace HdArnoldLight {

/// Returns an instance of HdArnoldLight for handling point lights.
///
/// @param delegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreatePointLight(HdArnoldRenderDelegate* delegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling distant lights.
///
/// @param delegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateDistantLight(HdArnoldRenderDelegate* delegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling disk lights.
///
/// @param delegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateDiskLight(HdArnoldRenderDelegate* delegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling rect lights.
///
/// @param delegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateRectLight(HdArnoldRenderDelegate* delegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling cylinder lights.
///
/// @param delegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateCylinderLight(HdArnoldRenderDelegate* delegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling dome lights.
///
/// @param delegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateDomeLight(HdArnoldRenderDelegate* delegate, const SdfPath& id);

/// Returns an instance of HdArnoldLight for handling simple lights.
///
/// @param delegate Pointer to the Render Delegate.
/// @param id Path to the Hydra Primitive.
/// @return Instance of HdArnoldLight.
HDARNOLD_API
HdLight* CreateSimpleLight(HdArnoldRenderDelegate* delegate, const SdfPath& id);

} // namespace HdArnoldLight

PXR_NAMESPACE_CLOSE_SCOPE
