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
#include "nodes.h"

#include "../constant_strings.h"

#include <array>
#include <tuple>

PXR_NAMESPACE_OPEN_SCOPE

extern const AtNodeMethods* HdArnoldDriverMtd;

namespace {
struct NodeDefinition {
    int type;
    uint8_t outputType;
    const AtString& name;
    const AtNodeMethods* methods;
};

using BuiltInNodes = std::vector<NodeDefinition>;

const auto builtInNodes = []() -> const BuiltInNodes& {
    static const BuiltInNodes ret{
        {AI_NODE_DRIVER, AI_TYPE_UNDEFINED, str::HdArnoldDriver, HdArnoldDriverMtd},
    };
    return ret;
};

} // namespace

void hdArnoldInstallNodes()
{
    for (const auto& it : builtInNodes()) {
        AiNodeEntryInstall(it.type, it.outputType, it.name, "<built-in>", it.methods, AI_VERSION);
    }
}

void hdArnoldUninstallNodes()
{
    for (const auto& it : builtInNodes()) {
        AiNodeEntryUninstall(it.name);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
