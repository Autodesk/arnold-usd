//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2026 Autodesk, Inc.
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
#include "gaussian_splat_color_space_adapter.h"

#if PXR_VERSION >= 2505
#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/usd/usd/colorSpaceAPI.h>
#include <pxr/usd/usd/prim.h>

#include <constant_strings.h>
#include <shape_utils.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = ArnoldGaussianSplatColorSpaceAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

#if PXR_VERSION >= 2505

namespace {

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (ParticleField3DGaussianSplat)
    (Points)
    ((gsAlpha, "primvars:GS_Alpha"))
    ((colorSpaceName, "colorSpace:name"))
    ((shCoeffs, "radiance:sphericalHarmonicsCoefficients"))
    ((shCoeffsh, "radiance:sphericalHarmonicsCoefficientsh"))
    ((displayColor, "primvars:displayColor"))
    ((displayColorPrimvar, "displayColor"))
);
// clang-format on

bool _IsGaussianSplat(const UsdPrim& prim)
{
    // This adapter runs on every prim, test the type name before looking up any attribute
    const TfToken& typeName = prim.GetTypeName();
    if (typeName == _tokens->ParticleField3DGaussianSplat)
        return true;
    // Houdini exports its splats as points with a GS_Alpha primvar
    return typeName == _tokens->Points && prim.HasAttribute(_tokens->gsAlpha);
}

/// Resolves the color space of an attribute only when it's queried.
class _ColorSpaceDataSource : public HdTypedSampledDataSource<TfToken> {
public:
    HD_DECLARE_DATASOURCE(_ColorSpaceDataSource);

    VtValue GetValue(HdSampledDataSource::Time shutterOffset) override
    {
        return VtValue(GetTypedValue(shutterOffset));
    }

    TfToken GetTypedValue(HdSampledDataSource::Time) override
    {
        return UsdColorSpaceAPI::ComputeColorSpaceName(_attr);
    }

    bool GetContributingSampleTimesForInterval(
        HdSampledDataSource::Time, HdSampledDataSource::Time, std::vector<HdSampledDataSource::Time>*) override
    {
        return false;
    }

private:
    _ColorSpaceDataSource(const UsdAttribute& attr) : _attr(attr) {}

    UsdAttribute _attr;
};

HdDataSourceLocator _ColorSpaceLocator(const TfToken& primvarName)
{
    return HdPrimvarsSchema::GetDefaultLocator().Append(primvarName).Append(str::t_colorSpace);
}

} // namespace

HdContainerDataSourceHandle ArnoldGaussianSplatColorSpaceAdapter::GetImagingSubprimData(
    const UsdPrim& prim, const TfToken& subprim, const TfToken& appliedInstanceName,
    const UsdImagingDataSourceStageGlobals&)
{
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty() || !_IsGaussianSplat(prim))
        return nullptr;
    // The overlay of the adapters' containers merges this colorSpace with the primvar's value, interpolation
    // and role. It's only added to an authored primvar, otherwise Hydra would see a primvar without a value.
    const UsdAttribute attr = ArnoldUsdGetGaussianSplatColorAttribute(prim);
    if (!attr || !attr.HasAuthoredValue())
        return nullptr;
    // UsdImaging already sets the color space of displayColor since USD 26.03, as it has the color role, but
    // not of the coefficients, which have no role. The float and half coefficients are both provided as the
    // radiance:sphericalHarmonicsCoefficients primvar.
    const TfToken& primvarName =
        attr.GetName() == _tokens->displayColor ? _tokens->displayColorPrimvar : _tokens->shCoeffs;
    return HdRetainedContainerDataSource::New(
        HdPrimvarsSchema::GetSchemaToken(),
        HdRetainedContainerDataSource::New(
            primvarName, HdRetainedContainerDataSource::New(str::t_colorSpace, _ColorSpaceDataSource::New(attr))));
}

HdDataSourceLocatorSet ArnoldGaussianSplatColorSpaceAdapter::InvalidateImagingSubprim(
    const UsdPrim& prim, const TfToken& subprim, const TfToken& appliedInstanceName,
    const TfTokenVector& properties, UsdImagingPropertyInvalidationType)
{
    HdDataSourceLocatorSet result;
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty() || !_IsGaussianSplat(prim))
        return result;
    for (const TfToken& property : properties) {
        // UsdImaging doesn't invalidate the primvars when colorSpace:name changes. The colorSpace metadata of
        // the color attributes also changes their color space, and which attribute holds the colors.
        if (property == _tokens->colorSpaceName || property == _tokens->shCoeffs ||
            property == _tokens->shCoeffsh || property == _tokens->displayColor) {
            result.insert(_ColorSpaceLocator(_tokens->shCoeffs));
            result.insert(_ColorSpaceLocator(_tokens->displayColorPrimvar));
            break;
        }
    }
    return result;
}

#endif // PXR_VERSION >= 2505

PXR_NAMESPACE_CLOSE_SCOPE
