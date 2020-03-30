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
/// @file instancer.h
///
/// Utilities to support point instancers.
#pragma once

#include "api.h"

#include <ai_matrix.h>

#include <pxr/pxr.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/instancer.h>

#include <mutex>
#include <unordered_map>
#include <vector>

#include "render_delegate.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for the point instancer.
class HdArnoldInstancer : public HdInstancer {
public:
    /// Creates an instance of HdArnoldInstancer.
    ///
    /// @param renderDelegate Pointer to the render delegate creating the
    ///  instancer.
    HDARNOLD_API
    HdArnoldInstancer(
        HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate, const SdfPath& id,
        const SdfPath& parentInstancerId = SdfPath());

    /// Destructor for HdArnoldInstancer.
    ~HdArnoldInstancer() override = default;

    /// Calculates the matrices for all instances for a given shape.
    ///
    /// Values are cached and only updated when the primvars are dirtied.
    ///
    /// @param prototypeId ID of the instanced shape.
    /// @return All the matrices for the shape.
    HDARNOLD_API
    VtMatrix4dArray CalculateInstanceMatrices(const SdfPath& prototypeId);

    using PrimvarMap = std::unordered_map<TfToken, VtValue, TfToken::HashFunctor>; ///< Type to store primvars.
protected:
    /// Syncs the primvars for the instancer.
    ///
    /// Safe to call on multiple threads.
    HDARNOLD_API
    void _SyncPrimvars();

    HdArnoldRenderDelegate* _delegate; ///< The active render delegate.
    std::mutex _mutex;                 ///< Mutex to safe-guard calls to _SyncPrimvars.

    PrimvarMap _primvars; ///< Generic map to store all the primvars.
};

PXR_NAMESPACE_CLOSE_SCOPE
