//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2023 Autodesk, Inc.
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

#include "procedural_custom_adapter.h"

#include <pxr/imaging/hd/material.h>

#include <pxr/usdImaging/usdImaging/indexProxy.h>

#include "constant_strings.h"
#include <iostream>

#include <pxr/usdImaging/usdImaging/tokens.h>
#include <pxr/usdImaging/usdImaging/primAdapter.h>

#if PXR_VERSION >= 2108

#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/ar/resolverScopedCache.h>

#include "material_param_utils.h"

#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = ArnoldProceduralCustomAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

#if PXR_VERSION >= 2108

SdfPath ArnoldProceduralCustomAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    // Called to populate the RenderIndex for this UsdPrim.
    // The adapter is expected to create one or more prims in the render index using the given proxy.
    SdfPath cachePath = _AddRprim(str::t_procedural_custom, prim, index, GetMaterialUsdPath(prim), instancerContext);
    return cachePath;
}

void ArnoldProceduralCustomAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    // For the given prim, variability is detected and stored in timeVaryingBits.
    BaseAdapter::TrackVariability(prim, cachePath, timeVaryingBits, instancerContext);

    for (const auto& attribute : prim.GetAttributes()) {
        if (TfStringStartsWith(attribute.GetName().GetString(), str::arnold_prefix)) {
            _IsVarying(
                prim, attribute.GetName(), HdChangeTracker::DirtyPrimvar, UsdImagingTokens->usdVaryingPrimvar,
                timeVaryingBits, false);
        }
    }
}

bool ArnoldProceduralCustomAdapter::IsSupported(const UsdImagingIndexProxy* index) const
{
    // We limit the node graph adapter to the Arnold render delegate, and by default we are checking
    // for the support of "ArnoldUsd". Note, "ArnoldUsd" is an RPrim.
    return index->IsRprimTypeSupported(str::t_ArnoldUsd);
}

#else

SdfPath ArnoldProceduralCustomAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    SdfPath cachePath = _AddRprim(str::t_procedural_custom, prim, index, GetMaterialUsdPath(prim), instancerContext);
    return cachePath;
}

void ArnoldProceduralCustomAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    // For the given prim, variability is detected and stored in timeVaryingBits.
    BaseAdapter::TrackVariability(prim, cachePath, timeVaryingBits, instancerContext);
    for (const auto& attribute : prim.GetAttributes()) {
        if (TfStringStartsWith(attribute.GetName().GetString(), str::arnold_prefix)) {
            _IsVarying(
                prim, attribute.GetName(), HdChangeTracker::DirtyPrimvar, UsdImagingTokens->usdVaryingPrimvar,
                timeVaryingBits, false);
        }
    }
}

bool ArnoldProceduralCustomAdapter::IsSupported(const UsdImagingIndexProxy* index) const
{
    TF_UNUSED(index);
    return false;
}

#endif

void ArnoldProceduralCustomAdapter::UpdateForTime(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    // Populates the cache for the given prim, time and requestedBits.
    BaseAdapter::UpdateForTime(prim, cachePath, time, requestedBits, instancerContext);

    UsdImagingPrimvarDescCache* primvarDescCache = _GetPrimvarDescCache();
    HdPrimvarDescriptorVector& primvars = primvarDescCache->GetPrimvars(cachePath);
    // For this particular node, we want to pass all the attributes starting with arnold:: as constant primvars, 
    // so we can access them in the delegate.
    for (const auto &attr: prim.GetAttributes()) {
        if (TfStringStartsWith(attr.GetNamespace().GetString(), str::arnold)) {
            _MergePrimvar(&primvars, attr.GetName(), HdInterpolationConstant);
        }
    }

    // TODO: For attributes which are supposed to have nodes in them but are string in the usd world, we probably need to add dependencies
}

HdDirtyBits ArnoldProceduralCustomAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName)
{
    if (propertyName == UsdGeomTokens->visibility) {
        return HdChangeTracker::DirtyVisibility;
    }

    // It the property is the node_entry, we make a special case as we'll have to reset the primvars and create a new node
    if (propertyName == str::arnold_node_entry) {
        return DirtyNodeEntry | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyTransform;
    }

    if (TfStringStartsWith(propertyName.GetString(), str::arnold)) {
        return HdChangeTracker::DirtyPrimvar;
    }
    // Allow base class to handle change processing.
    return BaseAdapter::ProcessPropertyChange(prim, cachePath, propertyName);
}
void ArnoldProceduralCustomAdapter::MarkTransformDirty(UsdPrim const& prim,
    SdfPath const& cachePath, UsdImagingIndexProxy* index) {
    index->MarkRprimDirty(cachePath, HdChangeTracker::DirtyTransform);
}

// is this called on the dependencies of the prim considered by this object ??
void ArnoldProceduralCustomAdapter::MarkDirty(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index)
{
    index->MarkRprimDirty(cachePath, dirty);
}

void ArnoldProceduralCustomAdapter::MarkMaterialDirty(
        UsdPrim const& prim,
        SdfPath const& cachePath,
        UsdImagingIndexProxy* index)
{
    MarkDirty(prim, cachePath, HdMaterial::DirtyResource, index);
}

void ArnoldProceduralCustomAdapter::_RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index)
{
    index->RemoveRprim(cachePath);
}

void
ArnoldProceduralCustomAdapter::ProcessPrimResync(
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
