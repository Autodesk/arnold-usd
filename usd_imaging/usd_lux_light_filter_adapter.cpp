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
#include "usd_lux_light_filter_adapter.h"

#if PXR_VERSION >= 2111
#include <pxr/usd/usdLux/lightAPI.h>    
#else
#include <pxr/usd/usdLux/light.h>
#endif
#include <pxr/usd/usdShade/material.h>

#include <pxr/usdImaging/usdImaging/indexProxy.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (arnold)
    (ArnoldUsd)
);
// clang-format on

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdImagingArnoldUsdLuxLightFilterAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

SdfPath UsdImagingArnoldUsdLuxLightFilterAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
#if PXR_VERSION >= 2105
    // _GetMaterialNetworkSelector is not available anymore, so we just check
    // if ArnoldUsd is supported.
    if(!index->IsRprimTypeSupported(_tokens->ArnoldUsd)) {
        return {};
    }
#else
    if (_GetMaterialNetworkSelector() != _tokens->arnold) {
        return {};
    }
#endif
    const auto parentPrim = prim.GetParent();
#if PXR_VERSION >= 2111
    UsdLuxLightAPI lightAPI(parentPrim);
#else
    UsdLuxLight lightAPI(parentPrim);
#endif
    if (!lightAPI) {
        return {};
    }
    const auto filtersRel = lightAPI.GetFiltersRel();
    SdfPathVector filters;
    filtersRel.GetTargets(&filters);
    for (const auto& filter : filters) {
        const auto materialPrim = prim.GetStage()->GetPrimAtPath(filter);
        if (materialPrim.IsA<UsdShadeMaterial>()) {
            auto materialAdapter = index->GetMaterialAdapter(materialPrim);
            if (materialAdapter) {
                materialAdapter->Populate(materialPrim, index, nullptr);
                // Since lights are not instanced, the cache path should be the same as the Light's path.
                index->AddDependency(parentPrim.GetPath(), materialPrim);
            }
        }
    }
    return {};
}

void UsdImagingArnoldUsdLuxLightFilterAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

void UsdImagingArnoldUsdLuxLightFilterAdapter::UpdateForTime(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

HdDirtyBits UsdImagingArnoldUsdLuxLightFilterAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName)
{
    return 0;
}

void UsdImagingArnoldUsdLuxLightFilterAdapter::MarkDirty(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index)
{
}

void UsdImagingArnoldUsdLuxLightFilterAdapter::_RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index) {}

PXR_NAMESPACE_CLOSE_SCOPE
