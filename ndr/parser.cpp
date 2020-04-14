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

#include "ndrarnold.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(NdrArnoldParserPlugin);

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (arnold)
    ((arnoldPrefix, "arnold:"))
    (binary));
// clang-format on

namespace {

// We have to subclass SdrShaderProperty, because it tries to read the SdfType
// from a token, and it doesn't support all the parameter types arnold does,
// like the 4 component color. Besides this, we also guarantee that the default
// value will match the SdfType, as the SdfType comes from the default value.
class ArnoldShaderProperty : public SdrShaderProperty {
public:
    ArnoldShaderProperty(
        const TfToken& name, const SdfValueTypeName& typeName, const VtValue& defaultValue, bool isOutput,
        size_t arraySize, const NdrTokenMap& metadata, const NdrTokenMap& hints, const NdrOptionVec& options)
        : SdrShaderProperty(name, typeName.GetAsToken(), defaultValue, isOutput, arraySize, metadata, hints, options),
          _typeName(typeName)
    {
    }

    const SdfTypeIndicator GetTypeAsSdfType() const override { return {_typeName, _typeName.GetAsToken()}; }

private:
    SdfValueTypeName _typeName;
};

} // namespace

NdrArnoldParserPlugin::NdrArnoldParserPlugin() {}

NdrArnoldParserPlugin::~NdrArnoldParserPlugin() {}

NdrNodeUniquePtr NdrArnoldParserPlugin::Parse(const NdrNodeDiscoveryResult& discoveryResult)
{
    auto shaderDefs = NdrArnoldGetShaderDefs();
    UsdPrim prim;
    // All shader names should be prefixed with `arnold:` but we double-check,
    // similarly to the render delegate, as older versions of Hydra did not validate
    // the node ids against the shader registry.
    if (TfStringStartsWith(discoveryResult.identifier.GetText(), _tokens->arnoldPrefix)) {
        prim = shaderDefs->GetPrimAtPath(
            SdfPath(TfStringPrintf("/%s", discoveryResult.identifier.GetText() + _tokens->arnoldPrefix.size())));
    } else {
        prim = shaderDefs->GetPrimAtPath(SdfPath(TfStringPrintf("/%s", discoveryResult.identifier.GetText())));
    }
    if (!prim) {
        return nullptr;
    }
    NdrPropertyUniquePtrVec properties;
    const auto props = prim.GetAuthoredProperties();
    properties.reserve(props.size());
    for (const auto& property : props) {
        const auto& propertyName = property.GetName();
        // In case `info:id` is set on the nodes.
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
        // The utility function takes care of the conversion and figuring out
        // parameter types, so we just have to blindly pass all required
        // parametrs.
        // TODO(pal): Read metadata and hints.
        properties.emplace_back(SdrShaderPropertyUniquePtr(new ArnoldShaderProperty(
            propertyName,                        // name
            propertyStack.back()->GetTypeName(), // type
            v,                                   // defaultValue
            false,                               // isOutput
            0,                                   // arraySize
            NdrTokenMap(),                       // metadata
            NdrTokenMap(),                       // hints
            NdrOptionVec()                       // options
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
#ifdef USD_HAS_NEW_SDR_NODE_CONSTRUCTOR
        discoveryResult.uri, // resolvedUri
#endif
        std::move(properties)));
}

const NdrTokenVec& NdrArnoldParserPlugin::GetDiscoveryTypes() const
{
    static const NdrTokenVec ret = {_tokens->arnold};
    return ret;
}

const TfToken& NdrArnoldParserPlugin::GetSourceType() const { return _tokens->arnold; }

PXR_NAMESPACE_CLOSE_SCOPE
