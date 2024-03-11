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
#include "arnold_options_adapter.h"
#include "constant_strings.h"
#include "parameters_utils.h"

#include <pxr/usdImaging/usdImaging/indexProxy.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (arnold)
    (ArnoldUsd)
    (ArnoldOptions)
    ((options, "/options"))


);
// clang-format on

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = ArnoldOptionsAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

SdfPath ArnoldOptionsAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    
#if PXR_VERSION >= 2105
    // _GetMaterialNetworkSelector is not available anymore, so we just check
    // if ArnoldUsd is supported.
    if (!index->IsRprimTypeSupported(_tokens->ArnoldUsd)) {
        return {};
    }
#else
    if (_GetMaterialNetworkSelector() != _tokens->arnold) {
        return {};
    }
#endif
    // Ignore primitives that are not called /options, 
    // as this is the name of the arnold options node 
    if (prim.GetPath().GetToken() != _tokens->options)
        return {};
    index->InsertSprim(_tokens->ArnoldOptions, prim.GetPath(), prim);

    UsdAttribute camAttr = prim.GetAttribute(str::t_arnold_camera);
    VtValue camVal;
        
    if (camAttr && camAttr.Get(&camVal)) {
        std::string cameraName = VtValueGetString(camVal);
        if (!cameraName.empty()) {
            UsdPrim cameraPrim = prim.GetStage()->GetPrimAtPath(SdfPath(cameraName));
            if (cameraPrim) {
                index->AddDependency(prim.GetPath(), cameraPrim);
            }
        }
    }
    
    return prim.GetPath();
}

void ArnoldOptionsAdapter::TrackVariability(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

void ArnoldOptionsAdapter::UpdateForTime(
    const UsdPrim& prim, const SdfPath& cachePath, UsdTimeCode time, HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
}

HdDirtyBits ArnoldOptionsAdapter::ProcessPropertyChange(
    const UsdPrim& prim, const SdfPath& cachePath, const TfToken& propertyName)
{
    return 0;
}

void ArnoldOptionsAdapter::MarkDirty(
    const UsdPrim& prim, const SdfPath& cachePath, HdDirtyBits dirty, UsdImagingIndexProxy* index)
{
}

void ArnoldOptionsAdapter::_RemovePrim(const SdfPath& cachePath, UsdImagingIndexProxy* index)
{
    index->RemoveSprim(_tokens->ArnoldOptions, cachePath);
}

bool ArnoldOptionsAdapter::IsSupported(const UsdImagingIndexProxy* index) const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
