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
/// @file scene_delegate/prim_adapter.h
///
/// Base adapter for converting Arnold nodes to Hydra primitives.
#include "api.h"

#include <pxr/pxr.h>

#include <pxr/base/tf/type.h>

PXR_NAMESPACE_OPEN_SCOPE

class ImagingArnoldSceneDelegate;

/// Base class for all prim adapters.
class ImagingArnoldPrimAdapter {
public:
};

class ImagingArnoldPrimAdapterFactoryBase : public TfType::FactoryBase {
public:
    virtual ImagingArnoldPrimAdapter* Create() const = 0;
};

template <class T>
class ImagingArnoldPrimAdapterFactory : public ImagingArnoldPrimAdapterFactoryBase {
public:
    ImagingArnoldPrimAdapter* Create() const override { return new T(); }
};

#define DEFINE_ADAPTER_FACTORY(ADAPTER)                                          \
    TF_REGISTRY_FUNCTION(TfType)                                                 \
    {                                                                            \
        using Adapter = ADAPTER;                                                 \
        auto t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>(); \
        t.SetFactory<ImagingArnoldPrimAdapterFactory<Adapter>>();                \
    }

PXR_NAMESPACE_CLOSE_SCOPE
