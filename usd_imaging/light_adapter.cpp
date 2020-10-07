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
#include "light_adapter.h"

#include <pxr/usd/usdLux/light.h>
#include <pxr/usd/usdShade/material.h>

#include <pxr/usdImaging/usdImaging/indexProxy.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
     (arnold)
);
// clang-format on

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdImagingArnoldLightAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

SdfPath UsdImagingArnoldLightAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    if (_GetMaterialNetworkSelector() != _tokens->arnold) {
        return {};
    }
    const auto parentPrim = prim.GetParent();
    UsdLuxLight lightAPI(parentPrim);
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

void UsdImagingArnoldLightAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

void UsdImagingArnoldLightAdapter::UpdateForTime(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

HdDirtyBits UsdImagingArnoldLightAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName)
{
    return 0;
}

void UsdImagingArnoldLightAdapter::MarkDirty(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index)
{
}

void UsdImagingArnoldLightAdapter::_RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index) {}

PXR_NAMESPACE_CLOSE_SCOPE
