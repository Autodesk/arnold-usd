// Copyright 2019 Luma Pictures
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
//
// Modifications Copyright 2019 Autodesk, Inc.
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
/// @file discovery.h
///
/// Ndr Discovery plugin for arnold shader nodes.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/usd/ndr/discoveryPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Ndr Discovery for arnold shader nodes.
class NdrArnoldDiscoveryPlugin : public NdrDiscoveryPlugin {
public:
    using Context = NdrDiscoveryPluginContext;

    /// Creates an instance of NdrArnoldDiscoveryPlugin.
    NDRARNOLD_API
    NdrArnoldDiscoveryPlugin();

    /// Destructor for NdrArnoldNodeDiscoveryPlugin.
    NDRARNOLD_API
    ~NdrArnoldDiscoveryPlugin() override;

    /// Discovers the arnold shaders.
    ///
    /// This includes all the built-in shaders, where the uri is set to <built-in>
    /// and all the arnold shaders found in ARNOLD_PLUGIN_PATH, where the uri
    /// is set to the library/osl file providing the shader.
    ///
    /// @param context NdrDiscvoeryPluginContext of the discovery process.
    /// @return List of the discovered arnold nodes.
    NDRARNOLD_API
    NdrNodeDiscoveryResultVec DiscoverNodes(const Context& context) override;

    /// Returns the URIs used to search for arnold shader nodes.
    ///
    /// @return All the paths from ARNOLD_PLUGIN_PATH.
    const NdrStringVec& GetSearchURIs() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE
