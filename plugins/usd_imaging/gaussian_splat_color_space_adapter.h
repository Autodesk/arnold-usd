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

/// UsdImaging only provides the color space of the primvars having a color role, which excludes the
/// spherical harmonics coefficients of the gaussian splats, and doesn't invalidate the primvars when the
/// prim's colorSpace:name changes. This keyless adapter adds the resolved color space of the splat colors
/// to the gaussian splat prims (ParticleField3DGaussianSplat and Houdini's splat points), as the
/// "arnoldGaussianSplatColorSpace" token data source, and dirties it with the primvars when it changes.
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
