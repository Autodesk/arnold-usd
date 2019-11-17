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
/*
 * TODO:
 *  - Properly parse type and array size.
 *  - Generate output types based on shader output type.
 * */
#include "parser.h"

#include <pxr/base/tf/staticTokens.h>

#include <pxr/usd/ndr/node.h>

#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <pxr/usd/sdf/propertySpec.h>

#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/property.h>

#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(NdrArnoldParserPlugin);

TF_DEFINE_PRIVATE_TOKENS(_tokens, (arnold)(binary));

NdrArnoldParserPlugin::NdrArnoldParserPlugin() {}

NdrArnoldParserPlugin::~NdrArnoldParserPlugin() {}

NdrNodeUniquePtr NdrArnoldParserPlugin::Parse(const NdrNodeDiscoveryResult& discoveryResult)
{
    auto shaderDefs = NdrArnoldGetShaderDefs();
    auto prim = shaderDefs->GetPrimAtPath(SdfPath(TfStringPrintf("/%s", discoveryResult.identifier.GetText() + 3)));
    if (!prim) {
        return nullptr;
    }
    NdrPropertyUniquePtrVec properties;
    const auto props = prim.GetAuthoredProperties();
    properties.reserve(props.size());
    for (const auto& property : props) {
        const auto& propertyName = property.GetName();
        if (TfStringContains(propertyName.GetString(), ":")) {
            continue;
        }
        const auto propertyStack = property.GetPropertyStack();
        if (propertyStack.empty()) {
            continue;
        }
        const auto attr = prim.GetAttribute(propertyName);
        VtValue v;
        attr.Get(&v);
        properties.emplace_back(SdrShaderPropertyUniquePtr(new SdrShaderProperty(
            propertyName,                                 // name
            propertyStack[0]->GetTypeName().GetAsToken(), // type
            v,                                            // defaultValue
            false,                                        // isOutput
            0,                                            // arraySize
            NdrTokenMap(),                                // metadata
            NdrTokenMap(),                                // hints
            NdrOptionVec()                                // options
            )));
    }
    return NdrNodeUniquePtr(new SdrShaderNode(
        discoveryResult.identifier,    // identifier
        discoveryResult.version,       // version
        discoveryResult.name,          // name
        discoveryResult.family,        // family
        discoveryResult.discoveryType, // context
        discoveryResult.sourceType,    // sourceType
        discoveryResult.uri,           // uri
        discoveryResult.uri,           // resolvedUri
        std::move(properties)));
}

const NdrTokenVec& NdrArnoldParserPlugin::GetDiscoveryTypes() const
{
    static const NdrTokenVec ret = {_tokens->arnold};
    return ret;
}

const TfToken& NdrArnoldParserPlugin::GetSourceType() const { return _tokens->arnold; }

PXR_NAMESPACE_CLOSE_SCOPE
