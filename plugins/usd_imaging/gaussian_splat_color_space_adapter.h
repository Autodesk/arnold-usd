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
/// @file gaussian_splat_color_space_adapter.h
///
/// Keyless API schema adapter providing the color space of gaussian splats to Hydra.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/usdImaging/usdImaging/apiSchemaAdapter.h>

PXR_NAMESPACE_OPEN_SCOPE

/// The gaussian splat schema has no color space attribute of its own: the color space of the splat colors follows the
/// UsdColorSpaceAPI rules (OpenUSD#3716 rejected a dedicated attribute as redundant with ColorSpaceAPI). UsdImaging
/// doesn't give it to Hydra:
///  - it only sets the colorSpace of the primvars having the color role, which excludes the spherical harmonics
///    coefficients (float3[] / half3[] without a role), even when ColorSpaceAPI is applied to the prim
///  - it has no ColorSpaceAPI adapter, and doesn't invalidate the primvars when colorSpace:name changes
/// This keyless adapter sets the colorSpace of the color primvar of the gaussian splat prims
/// (ParticleField3DGaussianSplat and Houdini's splat points): primvars/radiance:sphericalHarmonicsCoefficients/colorSpace, or
/// primvars/displayColor/colorSpace when the coefficients aren't authored. It's the field UsdImaging uses for the
/// color primvars, so this adapter can go if UsdImaging fills it for the coefficients. It also invalidates these
/// colorSpace locators when colorSpace:name or the color attributes change, which syncs the rprim primvars.
/// Changes of colorSpace:name on an ancestor don't update the splats inheriting it.
class ArnoldGaussianSplatColorSpaceAdapter : public UsdImagingAPISchemaAdapter {
public:
    using BaseAdapter = UsdImagingAPISchemaAdapter;

#if PXR_VERSION >= 2505
    USDIMAGINGARNOLD_API
    HdContainerDataSourceHandle GetImagingSubprimData(
        const UsdPrim& prim, const TfToken& subprim, const TfToken& appliedInstanceName,
        const UsdImagingDataSourceStageGlobals& stageGlobals) override;

    USDIMAGINGARNOLD_API
    HdDataSourceLocatorSet InvalidateImagingSubprim(
        const UsdPrim& prim, const TfToken& subprim, const TfToken& appliedInstanceName,
        const TfTokenVector& properties, UsdImagingPropertyInvalidationType invalidationType) override;
#endif
};

PXR_NAMESPACE_CLOSE_SCOPE
