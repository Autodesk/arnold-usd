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
/// @file native_rprim_adapter.h
///
/// Utilities for converting Arnold Schemas to Hydra prims.
#include <pxr/pxr.h>
#include "api.h"

#include <pxr/usdImaging/usdImaging/gprimAdapter.h>

PXR_NAMESPACE_OPEN_SCOPE

class UsdImagingArnoldShapeAdapter : public UsdImagingGprimAdapter {
public:
    using BaseAdapter = UsdImagingGprimAdapter;
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
};

PXR_NAMESPACE_CLOSE_SCOPE
