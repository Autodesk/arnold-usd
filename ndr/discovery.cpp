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
#include "discovery.h"

#include <pxr/base/arch/fileSystem.h>

#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>

#include "utils.h"

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (shader)
    (arnold)
    ((filename, "arnold:filename"))
);
// clang-format on

NDR_REGISTER_DISCOVERY_PLUGIN(NdrArnoldDiscoveryPlugin);

NdrNodeDiscoveryResultVec NdrArnoldDiscoveryPlugin::DiscoverNodes(const Context& context)
{
    NdrNodeDiscoveryResultVec ret;
    auto shaderDefs = NdrArnoldGetShaderDefs();
    for (const UsdPrim& prim : shaderDefs->Traverse()) {
        const auto shaderName = prim.GetName();
        TfToken filename("<built-in>");
        prim.GetMetadata(_tokens->filename, &filename);
        ret.emplace_back(
            NdrIdentifier(TfStringPrintf("arnold:%s", shaderName.GetText())),     // identifier
            NdrVersion(AI_VERSION_ARCH_NUM, AI_VERSION_MAJOR_NUM).GetAsDefault(), // version
            shaderName,                                                           // name
            shaderName,                                                           // family
            _tokens->arnold,                                                      // discoveryType
            _tokens->arnold,                                                      // sourceType
            filename,                                                             // uri
            filename                                                              // resolvedUri
        );
    }
    return ret;
}

const NdrStringVec& NdrArnoldDiscoveryPlugin::GetSearchURIs() const
{
    static const auto result = []() -> NdrStringVec {
        NdrStringVec ret = TfStringSplit(TfGetenv("ARNOLD_PLUGIN_PATH"), ARCH_PATH_LIST_SEP);
        ret.push_back("<built-in>");
        return ret;
    }();
    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
