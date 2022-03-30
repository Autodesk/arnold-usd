// Copyright 2020 Autodesk, Inc.
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
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/usdImaging/usdImaging/primAdapter.h>

PXR_NAMESPACE_OPEN_SCOPE

class ArnoldMeshLightAdapter : public UsdImagingPrimAdapter {
public:
    using BaseAdapter = UsdImagingPrimAdapter;

    /// Populate primitives in the usd imaging index proxy.
    ///
    /// @param prim USD Primitive of the ArnoldNodeGraph.
    /// @param index Pointer to the UsdImagingIndexProxy.
    /// @param instancerContext Pointer to the UsdImagingInstancerContext, unusued.
    /// @return Path to the primitive inserted into the UsdImagingIndex.
    USDIMAGINGARNOLD_API
    SdfPath Populate(
        const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext) override;

    /// Tracking time variability of the primitive.
    ///
    /// @param prim USD Primitive of the ArnoldNodeGraph.
    /// @param cachePath Path to the primitive in the UsdImaging cache.
    /// @param timeVaryingBits Output Pointer to the HdDirtyBits, to store which bits are time varying.
    /// @param instancerContext Pointer to the UsdImagingInstancerContext, unused.
    USDIMAGINGARNOLD_API
    void TrackVariability(
        const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
        const UsdImagingInstancerContext* instancerContext) const override;

    /// Update primitive for a given time code.
    ///
    /// @param prim USD Primitive of the ArnoldNodeGraph.
    /// @param cachePath Path to the primitive in the UsdImaging cache.
    /// @param time Time code of the update.
    /// @param requestedBits Which bits have changed.
    /// @param instancerContext Pointer to the UsdImagingInstancerContext, unused.
    USDIMAGINGARNOLD_API
    void UpdateForTime(
        const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
        const UsdImagingInstancerContext* instancerContext) const override;

    /// Process a propery change and return the dirty bits.
    ///
    /// @param prim USD Primitive of the ArnoldNodeGraph.
    /// @param cachePath Path to the primitive in the UsdImaging cache.
    /// @param propertyName Name of the property that has changed.
    /// @return HdDirtyBits representing the change.
    USDIMAGINGARNOLD_API
    HdDirtyBits ProcessPropertyChange(
        const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName) override;

    /// Marks the primitive dirty.
    ///
    /// @param prim USD Primitive of the ArnoldNodeGraph.
    /// @param cachePath Path to the primitive in the UsdImaging cache.
    /// @param dirty Which HdDirtyBits are marked dirty.
    /// @param index Pointer to the UsdImagingIndexProxy.
    USDIMAGINGARNOLD_API
    void MarkDirty(
        const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index) override;
    /// Tells if the primitive is supported by an UsdImagingIndex.
    ///
    /// @param index Pointer to the UsdImagingIndex.
    /// @return True if the primitive is supported, false otherwise.
    USDIMAGINGARNOLD_API
    bool IsSupported(const UsdImagingIndexProxy* index) const override;

#if PXR_VERSION >= 2105
    USDIMAGINGARNOLD_API
    /// Gets the value of the parameter named key for the given prim (which
    /// has the given cache path) and given time.
    ///
    /// @param prim Primitive to query the parameters from.
    /// @param cachePath Path to the value cache.
    /// @param key Parameter name to query.
    /// @param time Time to query the attribute at.
    /// @param outIndices Output array to store the indices for primvars.
    /// @return Return the value of the attribute, or an empty VtValue.
    VtValue Get(
        const UsdPrim& prim, const SdfPath& cachePath, const TfToken& key, UsdTimeCode time,
        VtIntArray* outIndices) const override;
#elif PXR_VERSION >= 2011
    /// Gets the value of the parameter named key for the given prim (which
    /// has the given cache path) and given time.
    ///
    /// @param prim Primitive to query the parameters from.
    /// @param cachePath Path to the value cache.
    /// @param key Parameter name to query.
    /// @param time Time to query the attribute at.
    /// @return Return the value of the attribute, or an empty VtValue.
    USDIMAGINGARNOLD_API
    VtValue Get(const UsdPrim& prim, const SdfPath& cachePath, const TfToken& key, UsdTimeCode time) const override;
#endif

private:
    /// Removes the primitive from the UsdImagingIndex.
    ///
    /// @param cachePath Path to the primitive in the UsdImaging cache.
    /// @param index Pointer to the UsdImagingIndex.
    USDIMAGINGARNOLD_API
    void _RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index) override;

};

PXR_NAMESPACE_CLOSE_SCOPE
