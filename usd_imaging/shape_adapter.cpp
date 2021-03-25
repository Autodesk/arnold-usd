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
#include "shape_adapter.h"

#include <pxr/usdImaging/usdImaging/indexProxy.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#include <common_bits.h>
#include <constant_strings.h>

PXR_NAMESPACE_OPEN_SCOPE

SdfPath UsdImagingArnoldShapeAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    const auto arnoldPrimType = ArnoldDelegatePrimType();
    if (!index->IsRprimTypeSupported(arnoldPrimType)) {
        return {};
    }

    return _AddRprim(arnoldPrimType, prim, index, GetMaterialUsdPath(prim), instancerContext);
}

void UsdImagingArnoldShapeAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    BaseAdapter::TrackVariability(prim, cachePath, timeVaryingBits, instancerContext);

    for (const auto& attribute : prim.GetAttributes()) {
        if (TfStringStartsWith(attribute.GetName().GetString(), str::arnold_prefix)) {
            _IsVarying(
                prim, attribute.GetName(), ArnoldUsdRprimBitsParams, UsdImagingTokens->usdVaryingPrimvar,
                timeVaryingBits, false);
        }
    }
}

HdDirtyBits UsdImagingArnoldShapeAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& property)
{
    return TfStringStartsWith(property.GetString(), str::arnold_prefix)
               ? ArnoldUsdRprimBitsParams
               : BaseAdapter::ProcessPropertyChange(prim, cachePath, property);
}

PXR_NAMESPACE_CLOSE_SCOPE
