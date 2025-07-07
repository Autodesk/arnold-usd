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
#include "shape_adapter.h"

#include <pxr/usd/usd/schemaRegistry.h>

#include <common_bits.h>
#include <constant_strings.h>
#include <pxr/imaging/hd/dirtyBitsTranslator.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/usdImaging/usdImaging/indexProxy.h>
#include <pxr/usdImaging/usdImaging/primAdapter.h>
#include <pxr/usdImaging/usdImaging/tokens.h>
#include <pxr/usdImaging/usdImaging/dataSourceGprim.h>
#include <shape_utils.h>

#if PXR_VERSION >= 2505 // Hydra2
#include <pxr/imaging/hd/utils.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/usdImaging/usdImaging/dataSourceMaterial.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#endif // PXR_VERSION >= 2505 // Hydra2

std::size_t hash_value(const AtString& s) { return s.hash(); }

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

VtValue UsdImagingArnoldShapeAdapter::Get(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& key, UsdTimeCode time, VtIntArray* outIndices) const
{
    if (key == str::t_arnold__attributes) {
        ArnoldUsdParamValueList params;
        params.reserve(_paramNames.size());
        for (const auto& paramName : _paramNames) {
            const auto attribute = prim.GetAttribute(paramName.first);
            VtValue value;
            if (attribute && attribute.HasAuthoredValue() && attribute.Get(&value, time)) {
                // We don't need to treat attributes that don't have any authored value ("Get" would still return true)
                params.emplace_back(paramName.second, value);
            }
        }
        // To avoid copying.
        return VtValue::Take(params);
    }
    return UsdImagingGprimAdapter::Get(prim, cachePath, key, time, outIndices);
}

void UsdImagingArnoldShapeAdapter::_CacheParamNames(const TfToken& arnoldTypeName)
{
    // We are caching the parameter names using the schema registry.
    auto& registry = UsdSchemaRegistry::GetInstance();
    const auto* primDefinition = registry.FindConcretePrimDefinition(arnoldTypeName);
    if (primDefinition == nullptr) {
        return;
    }
    for (const auto& propertyName : primDefinition->GetPropertyNames()) {
        if (TfStringStartsWith(propertyName, str::arnold_prefix.c_str()) &&
            !ArnoldUsdIgnoreUsdParameter(propertyName)) {
            _paramNames.emplace_back(
                propertyName, propertyName.GetString().substr(str::arnold_prefix.length()).c_str());
        }
    }
}
#if PXR_VERSION >= 2505 // Hydra2

// TODO custom Rbits for arnold

class ArnoldShapeDataSourcePrim : public UsdImagingDataSourceGprim { // also tried HdDataSourceLegacyPrim but no sceneDelegate available at this point
public:
    HD_DECLARE_DATASOURCE(ArnoldShapeDataSourcePrim);
    
    USDIMAGINGARNOLD_API
    TfTokenVector GetNames() override
    {
        TfTokenVector result = UsdImagingDataSourceGprim::GetNames();
        // Assuming primvars is already added by UsdImagingDataSourcePrim
        result.push_back(str::t_arnold__attributes);

        return result;
    }

    USDIMAGINGARNOLD_API
    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == str::t_arnold__attributes) {
            ArnoldUsdParamValueList params;
            params.reserve(_paramNames.size());
            for (const auto& paramName : _paramNames) {
                const auto attribute = _GetUsdPrim().GetAttribute(paramName.first);
                VtValue value;
                if (attribute && attribute.HasAuthoredValue() && attribute.Get(&value, _GetStageGlobals().GetTime())) {
                    // We don't need to treat attributes that don't have any authored value ("Get" would still return true)
                    params.emplace_back(paramName.second, value);
                }
            }
            // Ideally we should return an UsdImagingDataSourceAttribute per attribue, it takes care of setting invalidation flags: time varying, asset dependent, ...
            //return UsdImagingDataSourceAttribute<ArnoldUsdParamValueList>::New(params);
            return HdRetainedSampledDataSource::New(VtValue(params));
        }
        return UsdImagingDataSourceGprim::Get(name);
    }

    ~ArnoldShapeDataSourcePrim() override = default;

    static HdDataSourceLocatorSet Invalidate(
        const UsdPrim& prim, const TfToken& subprim, const TfTokenVector& properties,
        UsdImagingPropertyInvalidationType invalidationType)
    {
        HdDataSourceLocatorSet result =
            UsdImagingDataSourceGprim::Invalidate(prim, subprim, properties, invalidationType);
        if (std::any_of(properties.cbegin(), properties.cend(), [](const TfToken& propName) {
                return TfStringStartsWith(propName.GetString(), str::arnold_prefix);
            })) {
            result.insert(HdDataSourceLocator(str::t_arnold__attributes));
        }
        return result;
    }

private:
    ArnoldShapeDataSourcePrim(
        const SdfPath& sceneIndexPath, const UsdPrim& usdPrim, const ParamNamesT &paramNames, const UsdImagingDataSourceStageGlobals& stageGlobals)
        : UsdImagingDataSourceGprim(sceneIndexPath, usdPrim, stageGlobals), _paramNames(paramNames)
    {
        stageGlobals.FlagAsTimeVarying(sceneIndexPath, HdDataSourceLocator(str::t_arnold__attributes));
    }
    const ParamNamesT &_paramNames; ///< reference to the lookup table with USD and Arnold param names.
};

TfTokenVector UsdImagingArnoldShapeAdapter::GetImagingSubprims(UsdPrim const& prim) {
    // Assuming Arnold nodes are leaves
    return { TfToken() };
}

TfToken UsdImagingArnoldShapeAdapter::GetImagingSubprimType(UsdPrim const& prim, TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
         return ArnoldDelegatePrimType();
    }
    return UsdImagingGprimAdapter::GetImagingSubprimType(prim, subprim);
}

HdDataSourceLocatorSet UsdImagingArnoldShapeAdapter::InvalidateImagingSubprim(
    UsdPrim const& prim, TfToken const& subprim, TfTokenVector const& properties,
    UsdImagingPropertyInvalidationType invalidationType)
{
    HdDataSourceLocatorSet result;

    // Dirty our whole node graph
    if (result.IsEmpty() && subprim.IsEmpty()) {
        result.insert(ArnoldShapeDataSourcePrim::Invalidate(
            prim, subprim, properties, invalidationType));
    }

    return result;
}


HD_DECLARE_DATASOURCE_HANDLES(ArnoldShapeDataSourcePrim);

HdContainerDataSourceHandle UsdImagingArnoldShapeAdapter::GetImagingSubprimData(
    UsdPrim const& prim, TfToken const& subprim, const UsdImagingDataSourceStageGlobals& stageGlobals)
{
    if (subprim.IsEmpty()) {
        return ArnoldShapeDataSourcePrim::New(
            prim.GetPath(),
            prim,
            _paramNames,
            stageGlobals);
    }
    return UsdImagingGprimAdapter::GetImagingSubprimData(prim, subprim, stageGlobals);
}
#endif // PXR_VERSION >= 2505 // Hydra2



PXR_NAMESPACE_CLOSE_SCOPE
