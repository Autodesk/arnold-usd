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

#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/instancer.h>

#include <mutex>
#include <unordered_map>
#include <vector>

#include "render_delegate.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Utility class for the point instancer.
class HdArnoldInstancer : public HdInstancer {
public:
#if PXR_VERSION >= 2102
    /// Creates an instance of HdArnoldInstancer.
    ///
    /// @param renderDelegate Pointer to the render delegate creating the
    ///  instancer.
    /// @param sceneDelegate Pointer to Hydra Scene Delegate.
    /// @param id Path to the instancer.
    HDARNOLD_API
    HdArnoldInstancer(HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate, const SdfPath& id);
#else
    /// Creates an instance of HdArnoldInstancer.
    ///
    /// @param renderDelegate Pointer to the render delegate creating the
    ///  instancer.
    /// @param sceneDelegate Pointer to Hydra Scene Delegate.
    /// @param id Path to the instancer.
    /// @param parentInstanceId Path to the parent Instancer.
    HDARNOLD_API
    HdArnoldInstancer(
        HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate, const SdfPath& id,
        const SdfPath& parentInstancerId = SdfPath());
#endif

    /// Destructor for HdArnoldInstancer.
    ~HdArnoldInstancer() override = default;

    /// Calculates the matrices for all instances for a given shape, including sampling multiple times.
    ///
    /// @param prototypeId ID of the instanced shape.
    /// @param sampleArray Output struct to hold time sampled matrices.
    HDARNOLD_API
    void CalculateInstanceMatrices(const SdfPath& prototypeId, HdArnoldSampledMatrixArrayType& sampleArray);

    /// Sets the primvars on the instancer node.
    ///
    /// Nested instance parameters are not currently supported. If the instanceCount does not match the number
    /// of values in a primvar, the primvar is going to be ignored.
    ///
    /// @param node Pointer to the Arnold instancer node.
    /// @param prototypeId ID of the prototype being instanced.
    /// @param instanceCount Number of instances.
    HDARNOLD_API
    void SetPrimvars(AtNode* node, const SdfPath& prototypeId, size_t instanceCount);

protected:
    /// Syncs the primvars for the instancer.
    ///
    /// Safe to call on multiple threads.
    HDARNOLD_API
    void _SyncPrimvars();

    std::mutex _mutex;                                ///< Mutex to safe-guard calls to _SyncPrimvars.
    HdArnoldPrimvarMap _primvars;                     ///< Unordered map to store all the primvars.
    HdArnoldSampledType<VtMatrix4dArray> _transforms; ///< Sampled instance transform values.
    HdArnoldSampledType<VtVec3fArray> _translates;    ///< Sampled instance translate values.
    // Newer versions use GfQuatH arrays instead of GfVec4f arrays.
#if PXR_VERSION >= 2008
    HdArnoldSampledType<VtQuathArray> _rotates; ///< Sampled instance rotate values.
#else
    HdArnoldSampledType<VtVec4fArray> _rotates; ///< Sampled instance rotate values.
#endif
    HdArnoldSampledType<VtVec3fArray> _scales; ///< Sampled instance scale values.
};

PXR_NAMESPACE_CLOSE_SCOPE
