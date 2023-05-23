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
#include "node_graph_adapter.h"

#include <pxr/imaging/hd/material.h>

#include <pxr/usdImaging/usdImaging/indexProxy.h>

#include "constant_strings.h"

#if PXR_VERSION >= 2108

#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/ar/resolverScopedCache.h>

#include "material_param_utils.h"

#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = ArnoldNodeGraphAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

#if PXR_VERSION >= 2108

SdfPath ArnoldNodeGraphAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    TF_UNUSED(instancerContext);
    index->InsertSprim(HdPrimTypeTokens->material, prim.GetPath(), prim);

    // Also register dependencies on behalf of any descendent
    // UsdShadeShader prims, since they are consumed to
    // create the node network.
    for (UsdPrim const& child: prim.GetDescendants()) {
        if (child.IsA<UsdShadeShader>()) {
            index->AddDependency(prim.GetPath(), child);
        }
    }

    return prim.GetPath();
}

void ArnoldNodeGraphAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    TF_UNUSED(instancerContext);
    TF_UNUSED(cachePath);
    // TODO: This is checking for the connected parameters on the primitive, which is not exactly what we want.
    // So it would be better to check all the terminals, check for their time variability.
    // if (UsdImagingArnoldIsHdMaterialNetworkTimeVarying(prim)) {
    //     *timeVaryingBits |= HdMaterial::DirtyResource;
    // }
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

#else

SdfPath ArnoldNodeGraphAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    TF_UNUSED(prim);
    TF_UNUSED(index);
    TF_UNUSED(instancerContext);
    return {};
}

void ArnoldNodeGraphAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    TF_UNUSED(prim);
    TF_UNUSED(cachePath);
    TF_UNUSED(timeVaryingBits);
    TF_UNUSED(instancerContext);
}

bool ArnoldNodeGraphAdapter::IsSupported(const UsdImagingIndexProxy* index) const
{
    TF_UNUSED(index);
    return false;
}

#endif

void ArnoldNodeGraphAdapter::UpdateForTime(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

HdDirtyBits ArnoldNodeGraphAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName)
{
    if (propertyName == UsdGeomTokens->visibility) {
        // Materials aren't affected by visibility
        return HdChangeTracker::Clean;
    }

    // The only meaningful change is to dirty the computed resource,
    // an HdMaterialNetwork.
    return HdMaterial::DirtyResource;
}

void ArnoldNodeGraphAdapter::MarkDirty(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index)
{
    // If this is invoked on behalf of a Shader prim underneath a
    // Material prim, walk up to the enclosing Material.
    SdfPath arnoldNodeGraphCachePath = cachePath;
    UsdPrim arnoldNodeGraphPrim = prim;
    while (arnoldNodeGraphPrim && arnoldNodeGraphPrim.GetTypeName() != str::t_ArnoldNodeGraph) {
        arnoldNodeGraphPrim = arnoldNodeGraphPrim.GetParent();
        arnoldNodeGraphCachePath = arnoldNodeGraphCachePath.GetParentPath();
    }
    if (!TF_VERIFY(arnoldNodeGraphPrim)) {
        return;
    }

    index->MarkSprimDirty(arnoldNodeGraphCachePath, dirty);
}

void ArnoldNodeGraphAdapter::MarkMaterialDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index)
{
    MarkDirty(prim, cachePath, HdMaterial::DirtyResource, index);
}

void ArnoldNodeGraphAdapter::_RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index)
{
    index->RemoveSprim(HdPrimTypeTokens->material, cachePath);
}

void
ArnoldNodeGraphAdapter::ProcessPrimResync(
        SdfPath const& cachePath,
        UsdImagingIndexProxy *index)
{
    // Since we're resyncing a material, we can use the cache path as a
    // usd path.  We need to resync dependents to make sure rprims bound to
    // this material are resynced; this is necessary to make sure the material
    // is repopulated, since we don't directly populate materials.
#if PXR_VERSION >= 2108
    SdfPath const& usdPath = cachePath;
    _ResyncDependents(usdPath, index);
#endif

    UsdImagingPrimAdapter::ProcessPrimResync(cachePath, index);
}

PXR_NAMESPACE_CLOSE_SCOPE
