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

#include <pxr/usdImaging/usdImaging/indexProxy.h>

#include "constant_strings.h"
#include "material_param_utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (arnold)
    (ArnoldUsd)
);
// clang-format on

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
}

void ArnoldNodeGraphAdapter::UpdateForTime(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

HdDirtyBits ArnoldNodeGraphAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName)
{
    return 0;
}

void ArnoldNodeGraphAdapter::MarkDirty(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index)
{
}

void ArnoldNodeGraphAdapter::_RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index) {}

bool ArnoldNodeGraphAdapter::IsSupported(const UsdImagingIndexProxy* index) const
{
    // We limit the node graph adapter to the Arnold render delegate, and by default we are checking
    // for the support of "ArnoldUsd".
    return index->IsSprimTypeSupported(HdPrimTypeTokens->material) && index->IsSprimTypeSupported(str::t_ArnoldUsd);
}

PXR_NAMESPACE_CLOSE_SCOPE
