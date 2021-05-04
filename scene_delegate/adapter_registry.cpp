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
#include "adapter_registry.h"

#include <pxr/base/tf/instantiateSingleton.h>

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(ImagingArnoldAdapterRegistry);

ImagingArnoldAdapterRegistry::ImagingArnoldAdapterRegistry()
{
    PlugRegistry& plugRegistry = PlugRegistry::GetInstance();
    // We are querying all the types registered that inherit from ImagingArnoldPrimAdapter.
    const auto& adapterType = TfType::Find<ImagingArnoldPrimAdapter>();
    std::set<TfType> types;
    PlugRegistry::GetAllDerivedTypes(adapterType, &types);

    for (const auto& type : types) {
        auto plugin = plugRegistry.GetPluginForType(type);
        if (plugin == nullptr) {
            // Plugin can't be loaded for some reason.
            continue;
        }
        const auto& metadata = plugin->GetMetadataForType(type);
        const auto arnoldTypeName = metadata.find("arnoldTypeName");
        if (arnoldTypeName == metadata.end() || !arnoldTypeName->second.Is<std::string>()) {
            continue;
        }
        _typeMap.emplace(AtString{arnoldTypeName->second.Get<std::string>().c_str()}, type);
    }
}

ImagingArnoldAdapterRegistry::~ImagingArnoldAdapterRegistry() {}

ImagingArnoldPrimAdapterPtr ImagingArnoldAdapterRegistry::FindAdapter(const AtString& arnoldType) const
{
    auto type = _typeMap.find(arnoldType);
    if (type == _typeMap.end()) {
        return nullptr;
    }
    PlugRegistry& plugRegistry = PlugRegistry::GetInstance();
    auto plugin = plugRegistry.GetPluginForType(type->second);
    // Delay loading the plugin.
    if (!plugin || !plugin->Load()) {
        return nullptr;
    }

    auto* factory = type->second.GetFactory<ImagingArnoldPrimAdapterFactoryBase>();
    if (factory == nullptr) {
        return nullptr;
    }
    return factory->Create();
}

PXR_NAMESPACE_CLOSE_SCOPE
