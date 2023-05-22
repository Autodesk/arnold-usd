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

/// @class ImagingArnoldPrimAdapter
///
/// Base class for all prim adapters.
class ImagingArnoldPrimAdapter {
public:
    /// Tells if an adapter can work with a given Arnold scene delegate.
    ///
    /// This function typically checks if a given Hydra primitive type is supported by the render index.
    ///
    /// @param proxy Pointer to the ImagingArnoldDelegateProxy.
    /// @return True if the adapter works with the Arnold scene delegate, false otherwise.
    virtual bool IsSupported(ImagingArnoldDelegateProxy* proxy) const = 0;

    /// Populates a given Arnold scene delegate with the Hydra primitive required by the adapter.
    ///
    /// @param node Pointer to the Arnold node.
    /// @param proxy Pointer to the ImagingArnoldDelegateProxy.
    /// @param id Path of the Hydra primitive.
    virtual void Populate(AtNode* node, ImagingArnoldDelegateProxy* proxy, const SdfPath& id) = 0;

    /// Gets the mesh topology of an Arnold node.
    ///
    /// @param node Pointer to the Arnold node.
    /// @return Topology of a Hydra mesh.
    IMAGINGARNOLD_API
    virtual HdMeshTopology GetMeshTopology(const AtNode* node) const;

    /// Gets the transform of an Arnold node.
    ///
    /// @param node Pointer to the Arnold node.
    /// @return GfMatrix4d representing the transform.
    IMAGINGARNOLD_API
    GfMatrix4d GetTransform(const AtNode* node) const;

    /// Samples the transform of an Arnold node.
    ///
    /// Currently the function exits early if the Arnold node has more samples than maxSampleCount.
    ///
    /// @param node Pointer to the Arnold node.
    /// @param maxSampleCount Maximum number of samples to return.
    /// @param sampleTimes Output pointer to the float time of each sample.
    /// @param sampleValues Output pointer to the GfMatrix4d of each sample.
    /// @return Number of samples written to the output pointers.
    IMAGINGARNOLD_API
    size_t SampleTransform(
        const AtNode* node, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues) const;

    /// Gets the extent of an Arnold node.
    ///
    /// Currently always returns an extent of -AI_BIG..AI_BIG
    ///
    /// @param node Pointer to the Arnold node.
    /// @return Extent of the Arnold node.
    IMAGINGARNOLD_API
    virtual GfRange3d GetExtent(const AtNode* node) const;

    /// Gets the primvar descriptors of an Arnold node.
    ///
    /// @param node Pointer to the Arnold node.
    /// @param interpolation Interpolation of the primvar descriptors to query.
    /// @return Primvar descriptors of a given interpolation type, empty vector if none available.
    IMAGINGARNOLD_API
    virtual HdPrimvarDescriptorVector GetPrimvarDescriptors(const AtNode* node, HdInterpolation interpolation) const;

    /// Gets a named value from an Arnold node.
    ///
    /// @param node Pointer to the Arnold node.
    /// @param key Name of the value.
    /// @return Value of a given name named value, empty VtValue if not available.
    IMAGINGARNOLD_API
    virtual VtValue Get(const AtNode* node, const TfToken& key) const;
};

using ImagingArnoldPrimAdapterPtr = std::shared_ptr<ImagingArnoldPrimAdapter>;

/// @class ImagingArnoldPrimAdapterFactoryBase
///
/// Base factory to create an ImagingArnoldPrimAdapter for a given Arnold type.
class ImagingArnoldPrimAdapterFactoryBase : public TfType::FactoryBase {
public:
    /// Creates the ImagingArnoldPrimAdapter.
    ///
    /// @return Shared pointer to the ImagingArnoldPrimAdapter.
    virtual ImagingArnoldPrimAdapterPtr Create() const = 0;
};

/// @class ImagingArnoldPrimAdapterFactory
///
/// Utility class that creates a new instance of an adapter for a given Arnold type.
///
/// Use this factory for prim adapters that store data per Arnold node.
template <class T>
class ImagingArnoldPrimAdapterFactory : public ImagingArnoldPrimAdapterFactoryBase {
public:
    /// Creates the ImagingArnoldPrimAdapter.
    ///
    /// Creates a new instance of the adapter every time called.
    ///
    /// @return Shared pointer to the ImagingArnoldPrimAdapter.
    ImagingArnoldPrimAdapterPtr Create() const override { return std::make_shared<T>(); }
};

/// @class ImagingArnoldPrimAdapterFactory
///
/// Utility class that shared a single instance of an adapter for a given Arnold type.
///
/// Use this factory for prim adapters that don't store any data per Arnold node.
template <class T>
class ImagingArnoldPrimSharedAdapterFactory : public ImagingArnoldPrimAdapterFactoryBase {
public:
    /// Constructor of ImagingArnoldPrimSharedAdapterFactory.
    ImagingArnoldPrimSharedAdapterFactory() : _adapter(std::make_shared<T>()) {}

    /// Creates the ImagingArnoldPrimAdapter.
    ///
    /// Returns the same pointer every time called.
    ///
    /// @return Shared pointer to the ImagingArnoldPrimAdapter.
    ImagingArnoldPrimAdapterPtr Create() const override { return _adapter; }

private:
    /// Pointer to the shared ImagingArnoldPrimAdapter.
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
