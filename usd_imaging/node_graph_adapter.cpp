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
#include "node_graph_adapter.h"

#include <pxr/imaging/hd/material.h>

#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/ar/resolverScopedCache.h>

#include <pxr/usdImaging/usdImaging/indexProxy.h>

#include "constant_strings.h"
#include "material_param_utils.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = ArnoldNodeGraphAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

SdfPath ArnoldNodeGraphAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    index->InsertSprim(HdPrimTypeTokens->material, prim.GetPath(), prim);
    return prim.GetPath();
}

void ArnoldNodeGraphAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    // TODO: This is checking for the connected parameters on the primitive, which is not exactly what we want.
    // So it would be better to check all the terminals, check for their time variability.
    if (UsdImagingArnoldIsHdMaterialNetworkTimeVarying(prim)) {
        *timeVaryingBits |= HdMaterial::DirtyResource;
    }
}

void ArnoldNodeGraphAdapter::UpdateForTime(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

HdDirtyBits ArnoldNodeGraphAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName)
{
    return HdMaterial::AllDirty;
}

void ArnoldNodeGraphAdapter::MarkDirty(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index)
{
    // TODO: We need to mark the NodeGraph primitive dirty, if the dirty event receives one of the
    // UsdShade nodes underneath.
    // See pxr/usdImaging/usdImaging/material.cpp
    index->MarkSprimDirty(cachePath, dirty);
}

VtValue ArnoldNodeGraphAdapter::GetMaterialResource(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time) const
{
    // To do correct asset resolution.
    ArResolverContextBinder binder(prim.GetStage()->GetPathResolverContext());
    ArResolverScopedCache resolverCache;

    // TODO: replicate the code found in pxr/usd/usdShade/material.cpp (ComputeNamedOutputSources),
    // since this is simplified.
    HdMaterialNetworkMap materialNetworkMap;
    UsdShadeConnectableAPI connectableAPI(prim);
    if (!connectableAPI) {
        return VtValue{materialNetworkMap};
    }
    const auto outputs = connectableAPI.GetOutputs(true);
    for (auto output : outputs) {
        const auto sources = output.GetConnectedSources();
        if (sources.empty()) {
            continue;
        }
        UsdImagingArnoldBuildHdMaterialNetworkFromTerminal(
            sources[0].source.GetPrim(), output.GetBaseName(), TfTokenVector{}, TfTokenVector{}, &materialNetworkMap,
            time);
    }
    return VtValue{materialNetworkMap};
}

bool ArnoldNodeGraphAdapter::IsSupported(const UsdImagingIndexProxy* index) const
{
    // We limit the node graph adapter to the Arnold render delegate, and by default we are checking
    // for the support of "ArnoldUsd". Note, "ArnoldUsd" is an RPrim.
    return index->IsSprimTypeSupported(HdPrimTypeTokens->material) && index->IsRprimTypeSupported(str::t_ArnoldUsd);
}

void ArnoldNodeGraphAdapter::_RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index) {}

PXR_NAMESPACE_CLOSE_SCOPE
