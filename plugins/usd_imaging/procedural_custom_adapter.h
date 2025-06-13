//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2023 Autodesk, Inc.
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

#include <pxr/usdImaging/usdImaging/gprimAdapter.h>

PXR_NAMESPACE_OPEN_SCOPE

// The ArnoldProceduralCustom inherits from GprimAdapter as most of the code handling
// material assignement is already done in GprimAdapter. 
// Ideally the ProceduralCustomAdapter should just pass attributes and it would make sense to be an Sprim
class ArnoldProceduralCustomAdapter : public UsdImagingGprimAdapter {

public:
    using BaseAdapter = UsdImagingGprimAdapter;

    ArnoldProceduralCustomAdapter()
        : UsdImagingGprimAdapter()
    {}
#if PXR_VERSION >= 2505
    //
    // Scene index support
    //

    TfTokenVector GetImagingSubprims(UsdPrim const& prim) override;

    TfToken GetImagingSubprimType(UsdPrim const& prim, TfToken const& subprim) override;

    HdContainerDataSourceHandle GetImagingSubprimData(
        UsdPrim const& prim, TfToken const& subprim, const UsdImagingDataSourceStageGlobals& stageGlobals) override;
#endif

    /// Populate primitives in the usd imaging index proxy.
    ///
    /// @param prim USD Primitive of the ArnoldProceduralCustom.
    /// @param index Pointer to the UsdImagingIndexProxy.
    /// @param instancerContext Pointer to the UsdImagingInstancerContext, unusued.
    /// @return Path to the primitive inserted into the UsdImagingIndex.
    USDIMAGINGARNOLD_API
    SdfPath Populate(
        const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext) override;

    /// Tracking time variability of the primitive.
    ///
    /// @param prim USD Primitive of the ArnoldProceduralCustom.
    /// @param cachePath Path to the primitive in the UsdImaging cache.
    /// @param timeVaryingBits Output Pointer to the HdDirtyBits, to store which bits are time varying.
    /// @param instancerContext Pointer to the UsdImagingInstancerContext, unused.
    USDIMAGINGARNOLD_API
    void TrackVariability(
        const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
        const UsdImagingInstancerContext* instancerContext) const override;

    /// Update primitive for a given time code.
    ///
    /// @param prim USD Primitive of the ArnoldProceduralCustom.
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
    /// @param prim USD Primitive of the ArnoldProceduralCustom.
    /// @param cachePath Path to the primitive in the UsdImaging cache.
    /// @param propertyName Name of the property that has changed.
    /// @return HdDirtyBits representing the change.
    USDIMAGINGARNOLD_API
    HdDirtyBits ProcessPropertyChange(
        const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName) override;

    /// Marks the primitive dirty.
    ///
    /// @param prim USD Primitive of the ArnoldProceduralCustom.
    /// @param cachePath Path to the primitive in the UsdImaging cache.
    /// @param dirty Which HdDirtyBits are marked dirty.
    /// @param index Pointer to the UsdImagingIndexProxy.
    USDIMAGINGARNOLD_API
    void MarkDirty(
        const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index) override;

    USDIMAGINGARNOLD_API
    void MarkMaterialDirty(UsdPrim const& prim,
                           SdfPath const& cachePath,
                           UsdImagingIndexProxy* index) override;
    
    USDIMAGINGARNOLD_API
    void MarkTransformDirty(UsdPrim const& prim,
                            SdfPath const& cachePath,
                            UsdImagingIndexProxy* index) override;

    USDIMAGINGARNOLD_API
    void ProcessPrimResync(SdfPath const& cachePath,
                           UsdImagingIndexProxy* index) override;

    /// Tells if the primitive is supported by an UsdImagingIndex.
    ///
    /// @param index Pointer to the UsdImagingIndex.
    /// @return True if the primitive is supported, false otherwise.
    USDIMAGINGARNOLD_API
    bool IsSupported(const UsdImagingIndexProxy* index) const override;



private:
    /// Removes the primitive from the UsdImagingIndex.
    ///
    /// @param cachePath Path to the primitive in the UsdImaging cache.
    /// @param index Pointer to the UsdImagingIndex.
    USDIMAGINGARNOLD_API
    void _RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index) override;
    
    int DirtyNodeEntry = 1 << 25; // TODO This should be shared in common if that works
};

PXR_NAMESPACE_CLOSE_SCOPE
