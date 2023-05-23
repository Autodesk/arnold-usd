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
/// @file scene_delegate/adapter_registry.h
///
/// Registry for scene delegate adapters.
#pragma once
#include "api.h"

#include <pxr/pxr.h>

#include <pxr/base/tf/singleton.h>
#include <pxr/base/tf/type.h>

#include "prim_adapter.h"

#include <ai.h>

#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

/// @class ImagingArnoldAdapterRegistry
///
/// Singleton registry class for creating and loading imaging arnold adapters.
class ImagingArnoldAdapterRegistry : public TfSingleton<ImagingArnoldAdapterRegistry> {
private:
    friend class TfSingleton<ImagingArnoldAdapterRegistry>;
    ImagingArnoldAdapterRegistry();
    ~ImagingArnoldAdapterRegistry();

public:
    /// Gets an instance of the registry.
    ///
    /// @return Reference to singleton instance of the registry.
    static ImagingArnoldAdapterRegistry& GetInstance()
    {
        return TfSingleton<ImagingArnoldAdapterRegistry>::GetInstance();
    }

    /// Finds an adapter for an Arnold node type.
    ///
    /// @param arnoldType Type of the Arnold node.
    /// @return Shared pointer to the adapter for a given Arnold node, nullptr if no adapters are available for any
    /// given node type.
    IMAGINGARNOLD_API
    ImagingArnoldPrimAdapterPtr FindAdapter(const AtString& arnoldType) const;

private:
    using TypeMap = std::unordered_map<AtString, TfType, AtStringHash>;

    /// Unordered hash map holding all the registered types.
    TypeMap _typeMap;
};

IMAGINGARNOLD_API_TEMPLATE_CLASS(TfSingleton<ImagingArnoldAdapterRegistry>);

PXR_NAMESPACE_CLOSE_SCOPE
