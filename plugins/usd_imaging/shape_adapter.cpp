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

#include <pxr/usdImaging/usdImaging/indexProxy.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#include <common_bits.h>
#include <constant_strings.h>
#include <shape_utils.h>

#if PXR_VERSION >= 2011

std::size_t hash_value(const AtString& s) { return s.hash(); }

#endif

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

#if PXR_VERSION >= 2011
#if PXR_VERSION >= 2105
VtValue UsdImagingArnoldShapeAdapter::Get(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& key, UsdTimeCode time, VtIntArray* outIndices) const
#else
VtValue UsdImagingArnoldShapeAdapter::Get(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& key, UsdTimeCode time) const
#endif
{
    if (key == str::t_arnold__attributes) {
        ArnoldUsdParamValueList params;
        params.reserve(_paramNames.size());
        for (const auto& paramName : _paramNames) {
            const auto attribute = prim.GetAttribute(paramName.first);
            VtValue value;
            if (attribute && attribute.Get(&value, time)) {
                params.emplace_back(paramName.second, value);
            }
        }
        // To avoid copying.
        return VtValue::Take(params);
    }
#if PXR_VERSION >= 2105
    return UsdImagingGprimAdapter::Get(prim, cachePath, key, time, outIndices);
#else
    return UsdImagingGprimAdapter::Get(prim, cachePath, key, time);
#endif
}
#endif

void UsdImagingArnoldShapeAdapter::_CacheParamNames(const TfToken& arnoldTypeName)
{
#if PXR_VERSION >= 2011
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
#else
    TF_UNUSED(arnoldTypeName);
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE
