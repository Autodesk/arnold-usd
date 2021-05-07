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
#pragma once
#include "api.h"

#include <pxr/pxr.h>

#include <pxr/base/gf/range3d.h>

#include <pxr/base/tf/token.h>
#include <pxr/base/tf/type.h>

#include <pxr/usd/sdf/path.h>

#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/sceneDelegate.h>

#include <ai.h>

#include "delegate_proxy.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Base class for all prim adapters.
class ImagingArnoldPrimAdapter {
public:
    virtual bool IsSupported(ImagingArnoldDelegateProxy* proxy) const = 0;

    virtual void Populate(AtNode* node, ImagingArnoldDelegateProxy* proxy, const SdfPath& id) = 0;

    IMAGINGARNOLD_API
    virtual HdMeshTopology GetMeshTopology(const AtNode* node) const;

    IMAGINGARNOLD_API
    GfMatrix4d GetTransform(const AtNode* node) const;

    IMAGINGARNOLD_API
    size_t SampleTransform(
        const AtNode* node, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues) const;

    IMAGINGARNOLD_API
    virtual GfRange3d GetExtent(const AtNode* node) const;

    IMAGINGARNOLD_API
    virtual HdPrimvarDescriptorVector GetPrimvarDescriptors(const AtNode* node, HdInterpolation interpolation) const;

    IMAGINGARNOLD_API
    virtual VtValue Get(const AtNode* node, const TfToken& key) const;
};

using ImagingArnoldPrimAdapterPtr = std::shared_ptr<ImagingArnoldPrimAdapter>;

class ImagingArnoldPrimAdapterFactoryBase : public TfType::FactoryBase {
public:
    virtual ImagingArnoldPrimAdapterPtr Create() const = 0;
};

template <class T>
class ImagingArnoldPrimAdapterFactory : public ImagingArnoldPrimAdapterFactoryBase {
public:
    ImagingArnoldPrimAdapterPtr Create() const override { return std::make_shared<T>(); }
};

template <class T>
class ImagingArnoldPrimSharedAdapterFactory : public ImagingArnoldPrimAdapterFactoryBase {
public:
    ImagingArnoldPrimSharedAdapterFactory() : _adapter(std::make_shared<T>()) {}

    ImagingArnoldPrimAdapterPtr Create() const override { return _adapter; }

private:
    ImagingArnoldPrimAdapterPtr _adapter;
};

#define DEFINE_ADAPTER_FACTORY(ADAPTER)                                          \
    TF_REGISTRY_FUNCTION(TfType)                                                 \
    {                                                                            \
        using Adapter = ADAPTER;                                                 \
        auto t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>(); \
        t.SetFactory<ImagingArnoldPrimAdapterFactory<Adapter>>();                \
    }

#define DEFINE_SHARED_ADAPTER_FACTORY(ADAPTER)                                   \
    TF_REGISTRY_FUNCTION(TfType)                                                 \
    {                                                                            \
        using Adapter = ADAPTER;                                                 \
        auto t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>(); \
        t.SetFactory<ImagingArnoldPrimSharedAdapterFactory<Adapter>>();          \
    }

PXR_NAMESPACE_CLOSE_SCOPE
