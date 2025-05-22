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
#pragma once

#include "api.h"

#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>


PXR_NAMESPACE_USING_DIRECTIVE
#if PXR_VERSION < 2505
#include <pxr/usd/ndr/discoveryPlugin.h>
#include <pxr/usd/ndr/property.h>
#include <pxr/usd/ndr/parserPlugin.h>
using ShaderDiscoveryPlugin = NdrDiscoveryPlugin;
using ShaderDiscoveryPluginContext = NdrDiscoveryPluginContext;
using ShaderNodeDiscoveryResult = NdrNodeDiscoveryResult;
using ShaderPropertyUniquePtrVec = NdrPropertyUniquePtrVec;
using ShaderTokenMap = NdrTokenMap;
using ShaderNodeDiscoveryResultVec = NdrNodeDiscoveryResultVec;
using ShaderIdentifier = NdrIdentifier;
using ShaderVersion = NdrVersion;
using ShaderStringVec = NdrStringVec;
using ShaderParserPlugin = NdrParserPlugin;
using ShaderNodeUniquePtr = NdrNodeUniquePtr;
using ShaderTokenVec = NdrTokenVec;
using ShaderOptionVec = NdrOptionVec;
#define DISCOVERNODES_FUNC DiscoverNodes
#define PARSE_FUNC Parse
#else
#define USE_SDR_REGISTRY 1
#include <pxr/usd/sdr/discoveryPlugin.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/sdr/parserPlugin.h>
using ShaderDiscoveryPlugin = SdrDiscoveryPlugin;
using ShaderPropertyUniquePtrVec = SdrShaderPropertyUniquePtrVec;
using ShaderDiscoveryPluginContext = SdrDiscoveryPluginContext;
using ShaderNodeDiscoveryResult = SdrShaderNodeDiscoveryResult;
using ShaderTokenMap = SdrTokenMap;
using ShaderNodeDiscoveryResultVec = SdrShaderNodeDiscoveryResultVec;
using ShaderIdentifier = SdrIdentifier;
using ShaderVersion = SdrVersion;
using ShaderStringVec = SdrStringVec;
using ShaderParserPlugin = SdrParserPlugin;
using ShaderNodeUniquePtr = SdrShaderNodeUniquePtr;
using ShaderTokenVec = SdrTokenVec;
using ShaderOptionVec = SdrOptionVec;
#define DISCOVERNODES_FUNC DiscoverShaderNodes
#define PARSE_FUNC ParseShaderNode
#endif