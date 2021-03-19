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
#include "shape_adapter.h"

#include <pxr/usdImaging/usdImaging/indexProxy.h>

#include <constant_strings.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdImagingArnoldShapeAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
}

// Expanded version for reference.
/*ARCH_CONSTRUCTOR(RegisterNativeGprimAdapter, TF_REGISTRY_PRIORITY, TfType*)
{
    Tf_RegistryInit::Add("usdImagingArnold",
                         [](void*, void*) {
                           using Adapter = UsdImagingArnoldGprimAdapter;
                           TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
                           t.SetFactory<UsdImagingPrimAdapterFactory<Adapter>>();
     }, "TfType");
}
_ARCH_ENSURE_PER_LIB_INIT(Tf_RegistryStaticInit, _tfRegistryInit);*/


SdfPath UsdImagingArnoldShapeAdapter::Populate(
    const UsdPrim& prim, UsdImagingIndexProxy* index, const UsdImagingInstancerContext* instancerContext)
{
    /*if (!index->IsRprimTypeSupported(str::t_arnold_rprim)) {
        return {};
    }*/

    return {};
}

PXR_NAMESPACE_CLOSE_SCOPE
