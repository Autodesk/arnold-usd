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
/// @file parser.h
///
/// Ndr Parser plugin for arnold shader nodes.
#pragma once

#include <pxr/pxr.h>
#include "api.h"

#include <pxr/usd/ndr/parserPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Ndr Parser for arnold shader nodes.
class NdrArnoldParserPlugin : public NdrParserPlugin {
public:
    /// Creates an instance of NdrArnoldParserPlugin.
    NDRARNOLD_API
    NdrArnoldParserPlugin();

    /// Destructor for NdrArnoldParserPlugin.
    NDRARNOLD_API
    ~NdrArnoldParserPlugin() override;

    /// Parses a node discovery result to a NdrNode.
    ///
    /// @param discoveryResult NdrNodeDiscoveryResult returned by the discovery plugin.
    /// @return The parsed Ndr Node.
    NDRARNOLD_API
    NdrNodeUniquePtr Parse(const NdrNodeDiscoveryResult& discoveryResult) override;

    /// Returns all the supported discovery types.
    ///
    /// @return Returns "arnold" as the only supported discovery type.
    NDRARNOLD_API
    const NdrTokenVec& GetDiscoveryTypes() const override;

    /// Returns all the supported source types.
    ///
    /// @return Returns "arnold" as the only supported source type.
    NDRARNOLD_API
    const TfToken& GetSourceType() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE
