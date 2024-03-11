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
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/usdImaging/usdImaging/primAdapter.h>

PXR_NAMESPACE_OPEN_SCOPE

class ArnoldOptionsAdapter : public UsdImagingPrimAdapter {
public:
    using BaseAdapter = UsdImagingPrimAdapter;

    USDIMAGINGARNOLD_API
    SdfPath Populate(
        const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext) override;

    USDIMAGINGARNOLD_API
    void TrackVariability(
        const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
        const UsdImagingInstancerContext* instancerContext) const override;

    USDIMAGINGARNOLD_API
    void UpdateForTime(
        const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
        const UsdImagingInstancerContext* instancerContext) const override;

    USDIMAGINGARNOLD_API
    HdDirtyBits ProcessPropertyChange(
        const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName) override;

    USDIMAGINGARNOLD_API
    void MarkDirty(
        const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index) override;

    USDIMAGINGARNOLD_API
    void _RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index) override;

    USDIMAGINGARNOLD_API
    bool IsSupported(const UsdImagingIndexProxy* index) const override;

};

PXR_NAMESPACE_CLOSE_SCOPE
