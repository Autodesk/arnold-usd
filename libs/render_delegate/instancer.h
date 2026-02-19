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
    /// Creates an instance of HdArnoldInstancer.
    ///
    /// @param renderDelegate Pointer to the render delegate creating the
    ///  instancer.
    /// @param sceneDelegate Pointer to Hydra Scene Delegate.
    /// @param id Path to the instancer.
    HDARNOLD_API
    HdArnoldInstancer(HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate, const SdfPath& id);

    /// Destructor for HdArnoldInstancer.
    ~HdArnoldInstancer() override = default;
    HDARNOLD_API
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override;
    /// Calculates the matrices for all instances for a given shape, including sampling multiple times.
    ///
    /// @param prototypeId ID of the instanced shape.
    /// @param sampleArray Output struct to hold time sampled matrices.
    HDARNOLD_API
    void CreateArnoldInstancer(HdArnoldRenderDelegate* renderDelegate, 
        const SdfPath& prototypeId, std::vector<AtNode *> &instancers);


    HDARNOLD_API
    bool ComputeShapeInstancesTransforms(HdArnoldRenderDelegate* renderDelegate, const SdfPath& prototypeId, AtNode *prototypeNode);

    HDARNOLD_API
    void ComputeShapeInstancesPrimvars(HdArnoldRenderDelegate* renderDelegate, const SdfPath& prototypeId, AtNode *prototypeNode);

    HDARNOLD_API
    void ApplyInstancerVisibilityToArnoldNode(AtNode *node);

    /// Sets the primvars on the instancer node.
    ///
    /// Nested instance parameters are not currently supported. If the instanceCount does not match the number
    /// of values in a primvar, the primvar is going to be ignored.
    ///
    /// @param node Pointer to the Arnold instancer node.
    /// @param prototypeId ID of the prototype being instanced.
    /// @param totalInstanceCount Total number of instances.
    /// @param childInstanceCount Number of child instances (for nested instancers)
    /// @param parentInstanceCount Returns the number of parent instances (for nested instancers)
    HDARNOLD_API
    void SetPrimvars(AtNode* node, const SdfPath& prototypeId, size_t totalInstanceCount, HdArnoldRenderDelegate* renderDelegate);

protected:
    /// Syncs the primvars for the instancer.
    ///
    /// Safe to call on multiple threads.
    HDARNOLD_API
    void _SyncPrimvars(HdDirtyBits dirtyBits, HdArnoldRenderParam* renderParam);

    /// Saves the sampling interval used for sampling the primvars related to the transform
    /// Returns true if the value has changed
    inline bool UpdateSamplingInterval(GfVec2f samplingInterval)
    {
        bool hasChanged = samplingInterval != _samplingInterval;
        _samplingInterval = samplingInterval;
        return hasChanged;
    }
    /// @brief Resamples the stored sampled primvars. This is necessary when the sampling interval has changed
    void ResampleInstancePrimvars();

    HdArnoldRenderDelegate* _delegate = nullptr;
    std::mutex _mutex;                                ///< Mutex to safe-guard calls to _SyncPrimvars.
    HdArnoldPrimvarMap _primvars;                     ///< Unordered map to store all the primvars.
    HdArnoldSampledType<VtMatrix4dArray> _transforms; ///< Sampled instance transform values.
    HdArnoldSampledType<VtVec3fArray> _translates;    ///< Sampled instance translate values.
    // Newer versions use GfQuatH arrays instead of GfVec4f arrays.
    HdArnoldSampledType<VtQuathArray> _rotates; ///< Sampled instance rotate values.
    HdArnoldSampledType<VtVec3fArray> _scales; ///< Sampled instance scale values.
    int _deformKeys = -1; ///< Number of samples to consider, -1 means deactivated

private:
    void ComputeSampleMatrixArray(HdArnoldRenderDelegate* renderDelegate, const VtIntArray &instanceIndices, HdArnoldSampledMatrixArrayType &sampleArray);

    GfVec2f _samplingInterval = {0.f, 0.f}; //< Keep track of the primvar sampling interval used 

};

PXR_NAMESPACE_CLOSE_SCOPE
