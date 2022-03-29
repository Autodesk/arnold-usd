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
#include "arnold_mesh_light_adapter.h"

#include <pxr/usd/usdLux/geometryLight.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/usdImaging/usdImaging/indexProxy.h>
#include <constant_strings.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (arnold)
    (ArnoldUsd)
    (GeometryLight)
);
// clang-format on

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = ArnoldMeshLightAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}


SdfPath ArnoldMeshLightAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    index->InsertSprim(_tokens->GeometryLight, prim.GetPath(), prim);

    UsdLuxGeometryLight light(prim);
    UsdRelationship rel = light.GetGeometryRel();
    SdfPathVector targets;
    rel.GetTargets(&targets);
    for (const auto &target : targets) {
        UsdPrim targetPrim = prim.GetStage()->GetPrimAtPath(target);
        if (targetPrim) {
            index->AddDependency(prim.GetPath(), targetPrim);
        }
    }
    return prim.GetPath();
}

void ArnoldMeshLightAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    TF_UNUSED(instancerContext);
    TF_UNUSED(cachePath);

    // Discover time-varying transforms.
    _IsTransformVarying(prim,
        HdLight::DirtyBits::DirtyTransform,
        UsdImagingTokens->usdVaryingXform,
        timeVaryingBits);

    // Discover time-varying visibility.
    _IsVarying(prim,
        UsdGeomTokens->visibility,
        HdLight::DirtyBits::DirtyParams,
        UsdImagingTokens->usdVaryingVisibility,
        timeVaryingBits,
        true);
    

    // If any of the light attributes is time varying 
    // we will assume all light params are time-varying.
    const std::vector<UsdAttribute> &attrs = prim.GetAttributes();
    for (UsdAttribute const& attr : attrs) {
        // Don't double-count transform attrs.
        if (UsdGeomXformable::IsTransformationAffectedByAttrNamed(
                attr.GetBaseName())) {
            continue;
        }
        if (attr.GetNumTimeSamples()>1){
            *timeVaryingBits |= HdLight::DirtyBits::DirtyParams;
            break;
        }
    }

#if PXR_VERSION >= 2105
    // Establish a primvar desc cache entry.
    HdPrimvarDescriptorVector& vPrimvars = _GetPrimvarDescCache()->GetPrimvars(cachePath);
#else
    UsdImagingValueCache* valueCache = _GetValueCache();
#endif
   
    // Compile a list of primvars to check.
    std::vector<UsdGeomPrimvar> primvars;
    UsdImaging_InheritedPrimvarStrategy::value_type inheritedPrimvarRecord =
        _GetInheritedPrimvars(prim.GetParent());
    if (inheritedPrimvarRecord) {
        primvars = inheritedPrimvarRecord->primvars;
    }

    UsdGeomPrimvarsAPI primvarsAPI(prim);
    std::vector<UsdGeomPrimvar> local = primvarsAPI.GetPrimvarsWithValues();
    primvars.insert(primvars.end(), local.begin(), local.end());
    for (auto const &pv : primvars) {
#if PXR_VERSION >= 2105
        _ComputeAndMergePrimvar(prim, pv, UsdTimeCode(), &vPrimvars);
#else
        _ComputeAndMergePrimvar(prim, cachePath, pv, UsdTimeCode(), valueCache);
#endif
    }

    // TODO: This is checking for the connected parameters on the primitive, which is not exactly what we want.
    // So it would be better to check all the terminals, check for their time variability.
    // if (UsdImagingArnoldIsHdMaterialNetworkTimeVarying(prim)) {
    //     *timeVaryingBits |= HdMaterial::DirtyResource;
    // }
}

bool ArnoldMeshLightAdapter::IsSupported(const UsdImagingIndexProxy* index) const
{   
    return index->IsSprimTypeSupported(_tokens->GeometryLight);
}


void ArnoldMeshLightAdapter::UpdateForTime(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}
HdDirtyBits ArnoldMeshLightAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName)
{
    return HdLight::AllDirty;
}


void ArnoldMeshLightAdapter::MarkDirty(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index)
{
    index->MarkSprimDirty(cachePath, dirty);
}
 
void ArnoldMeshLightAdapter::_RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index)
{
    index->RemoveSprim(_tokens->GeometryLight, cachePath);
}

#if PXR_VERSION >= 2105
VtValue ArnoldMeshLightAdapter::Get(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& key, UsdTimeCode time, VtIntArray* outIndices) const
#else

VtValue ArnoldMeshLightAdapter::Get(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& key, UsdTimeCode time) const
#endif
{    
    if (key == str::t_geometry)
    {
        std::string serializedGeom;
        UsdLuxGeometryLight light(prim);
        UsdRelationship rel = light.GetGeometryRel();
        SdfPathVector targets;
        rel.GetTargets(&targets);
        if (targets.empty()) {
            return VtValue();
        }
        return VtValue(_ConvertCachePathToIndexPath(targets[0]));
    }
#if PXR_VERSION >= 2105
    return UsdImagingPrimAdapter::Get(prim, cachePath, key, time, outIndices);
#else
    return UsdImagingPrimAdapter::Get(prim, cachePath, key, time);
#endif
}



PXR_NAMESPACE_CLOSE_SCOPE
