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

class UsdImagingArnoldLightFilterAPIAdapter : public UsdImagingPrimAdapter {
public:
    using BaseAdapter = UsdImagingPrimAdapter;

    UsdImagingArnoldLightFilterAPIAdapter() = default;
    ~UsdImagingArnoldLightFilterAPIAdapter() = default;

    virtual SdfPath Populate(UsdPrim const& prim,
                             UsdImagingIndexProxy* index,
                             UsdImagingInstancerContext const* instancerContext = NULL) = 0;

    virtual void TrackVariability(UsdPrim const& prim,
                                  SdfPath const& cachePath,
                                  HdDirtyBits* timeVaryingBits,
                                  UsdImagingInstancerContext const*
                                  instancerContext = NULL) const = 0;

    virtual void UpdateForTime(UsdPrim const& prim,
                               SdfPath const& cachePath,
                               UsdTimeCode time,
                               HdDirtyBits requestedBits,
                               UsdImagingInstancerContext const*
                               instancerContext = NULL) const = 0;

    virtual HdDirtyBits ProcessPropertyChange(UsdPrim const& prim,
                                              SdfPath const& cachePath,
                                              TfToken const& propertyName) = 0;

    virtual void MarkDirty(UsdPrim const& prim,
                           SdfPath const& cachePath,
                           HdDirtyBits dirty,
                           UsdImagingIndexProxy* index) = 0;

    virtual void _RemovePrim(SdfPath const& cachePath,
                             UsdImagingIndexProxy* index) = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE
