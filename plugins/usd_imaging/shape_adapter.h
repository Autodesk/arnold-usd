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
/// @file native_rprim_adapter.h
///
/// Utilities for converting Arnold Schemas to Hydra prims.
#include <pxr/pxr.h>
#include "api.h"

#include <pxr/usdImaging/usdImaging/gprimAdapter.h>

#include <ai.h>

    
PXR_NAMESPACE_OPEN_SCOPE

using ParamNamesT = std::vector<std::pair<TfToken, AtString>>;

class UsdImagingArnoldShapeAdapter : public UsdImagingGprimAdapter {
public:
    using BaseAdapter = UsdImagingGprimAdapter;

    UsdImagingArnoldShapeAdapter()
        : UsdImagingGprimAdapter()
    {}

    //
    // Scene index support
    //

    TfTokenVector GetImagingSubprims(UsdPrim const& prim) override;

    TfToken GetImagingSubprimType(UsdPrim const& prim, TfToken const& subprim) override;

    HdContainerDataSourceHandle GetImagingSubprimData(
        UsdPrim const& prim, TfToken const& subprim, const UsdImagingDataSourceStageGlobals& stageGlobals) override;

    USDIMAGINGARNOLD_API
    SdfPath Populate(
        const UsdPrim& prim, UsdImagingIndexProxy* index,
        const UsdImagingInstancerContext* instancerContext = nullptr) override;

    virtual TfToken ArnoldDelegatePrimType() const = 0;

    /// Thread Safe.
    USDIMAGINGARNOLD_API
    void TrackVariability(
        const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
        const UsdImagingInstancerContext* instancerContext = nullptr) const override;

    USDIMAGINGARNOLD_API
    HdDirtyBits ProcessPropertyChange(const UsdPrim& prim, const SdfPath& cachePath, const TfToken& property) override;
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
private:
    ParamNamesT _paramNames; ///< Lookup table with USD and Arnold param names.
protected:
    /// Caches param names for later lookup.
    /// Does nothing if USD is earlier than 20.11.
    ///
    /// @param arnoldTypeName Type of the arnold node.
    USDIMAGINGARNOLD_API
    void _CacheParamNames(const TfToken& arnoldTypeName);
};

PXR_NAMESPACE_CLOSE_SCOPE
