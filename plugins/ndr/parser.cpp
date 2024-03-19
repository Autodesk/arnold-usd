//
// SPDX-License-Identifier: Apache-2.0
//

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
// Modifications Copyright 2022 Autodesk, Inc.
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
    ((outputsPrefix, "outputs:"))
    ((uigroups, "ui:groups"))
    (uimin)
    (uimax)
    (uisoftmin)
    (uisoftmax)
    (enumValues)
    (attrsOrder)
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
        const TfToken& name, const SdfValueTypeName& typeName, const TfToken& typeToken, const VtValue& defaultValue, bool isOutput,
        size_t arraySize, const NdrTokenMap& metadata, const NdrTokenMap& hints, const NdrOptionVec& options)
        : SdrShaderProperty(name, typeToken, defaultValue, isOutput, arraySize, metadata, hints, options),
          _typeName(typeName)
    {
    }

#if PXR_VERSION >= 2108
    const NdrSdfTypeIndicator
#else
    const SdfTypeIndicator
#endif
    GetTypeAsSdfType() const override
    {
        // Asset attributes are supposed to be declared as strings, but this function
        // should still return an asset type name #1755
        if (_typeName == SdfValueTypeNames->String && IsAssetIdentifier())
            return {SdfValueTypeNames->Asset, SdfValueTypeNames->Asset.GetAsToken()};
        
        return {_typeName, _typeName.GetAsToken()};
    }

#if PXR_VERSION >= 2111
    const VtValue& GetDefaultValueAsSdfType() const override { return _defaultValue; }
#endif

private:
    SdfValueTypeName _typeName;
};

} // namespace

NdrArnoldParserPlugin::NdrArnoldParserPlugin() {}

NdrArnoldParserPlugin::~NdrArnoldParserPlugin() {}


void _ReadShaderAttribute(const UsdAttribute &attr, NdrPropertyUniquePtrVec &properties, const std::string &folder)
{
    const TfToken& attrName = attr.GetName();
    const std::string& attrNameStr = attrName.GetString();

    // check if this attribute is an output #1121
    bool isOutput = TfStringStartsWith(attrName, _tokens->outputsPrefix);
    if (!isOutput && TfStringContains(attrNameStr, ":")) {
        // In case `info:id` is set on the nodes.
        return;
    }    
    SdfValueTypeName typeName = attr.GetTypeName();
    TfToken typeToken = typeName.GetAsToken();

    bool isAsset = false;
    VtValue v;
    
    // The utility function takes care of the conversion and figuring out
    // parameter types, so we just have to blindly pass all required
    // parameters.
    VtDictionary customData = attr.GetCustomData();
    NdrTokenMap metadata;
    NdrTokenMap hints;

    // For enum attributes, all enum fields should be set as "options"
    // to this attribute
    NdrOptionVec options;
    auto it = customData.find(_tokens->enumValues);
    if (it != customData.end()) {
        const VtStringArray &enumValues = it->second.Get<VtStringArray>();
        for (const auto &enumValue : enumValues) {
            TfToken enumValTf(enumValue.c_str());
            options.emplace_back(enumValTf, enumValTf);
        }
    }

    // USD supports a builtin list of metadatas. Only these ones
    // need to be declared in the "metadata" section.
    static const auto supportedMetadatas = {
        SdrPropertyMetadata->Page, 
        SdrPropertyMetadata->Connectable,
        SdrPropertyMetadata->Label,
        SdrPropertyMetadata->Role,
        SdrPropertyMetadata->Help,
        SdrPropertyMetadata->IsAssetIdentifier
    };

    if (!folder.empty()) {
        metadata.insert({SdrPropertyMetadata->Page, folder});
    }
    for (const auto &m : supportedMetadatas) {
        const auto it = customData.find(m);
        if (it != customData.end()) {
            if (m == SdrPropertyMetadata->IsAssetIdentifier)
                isAsset = true;

            metadata.insert({m, TfStringify(it->second)});
        }
    }

    // For metadatas that aren't USD builtins, we need to set
    // them as "hints", otherwise USD will complain
    for (const auto &it : customData) {
        // enumValues was handled above
        if (it.first == _tokens->enumValues || 
            (std::find(supportedMetadatas.begin(), 
            supportedMetadatas.end(), it.first) != supportedMetadatas.end()))
            continue;

        hints.insert({TfToken(it.first), TfStringify(it.second)});
    }
    // We're explicitely using token types for closures, to be consistent with other shader libraries.
    // But the declared type must be "Terminal"
    if (typeName == SdfValueTypeNames->Token) {
        typeToken = SdrPropertyTypes->Terminal;
    }

    // Asset attributes have to be treated differently as they need to be considered 
    // as strings in some parts of USD, but as assets in others. Since in practice, 
    // these attributes always default to empty strings, it's better not to set the
    // VtValue at all, so that we don't get errors about invalid types.
    if (!isAsset)
        attr.Get(&v);
        
    properties.emplace_back(SdrShaderPropertyUniquePtr(new ArnoldShaderProperty{
        isOutput ? attr.GetBaseName() : attr.GetName(), // name
        typeName,           // typeName
        typeToken,          // typeToken
        v,                  // defaultValue
        isOutput,           // isOutput
        0,                  // arraySize
        metadata,           // metadata
        hints,              // hints
        options             // options
    }));
}

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

    VtDictionary primCustomData = prim.GetCustomData();
    // keep track of which attributes were declared, to avoid 
    // doing it twice for the same parameter
    std::unordered_set<std::string> declaredAttributes;

    // If this node entry has a metadata ui.groups, we use it to determine
    // the UI grouping and ordering. We will expect it to be defined as e.g. :
    // "Base: base base_color metalness, Specular: specular specular_color"
    if (primCustomData.find(_tokens->uigroups) != primCustomData.end()) {
        VtValue groupsVal = primCustomData[_tokens->uigroups];
        // Get ui.groups metadata as a string
        std::string uigroupsStr = groupsVal.Get<std::string>();
        if (!uigroupsStr.empty()) {
            // First, split for each group, with the "," separator
            const auto uigroupList = TfStringSplit(uigroupsStr, ",");
            for (const auto& uigroupElem : uigroupList) {
                if (uigroupElem.empty())
                    continue;
                // The first part before ":" represents the group UI label,
                // the second contains the ordered list of attributes included 
                // in this group
                const auto uigroupSplit = TfStringSplit(uigroupElem, ":");
                // If no ":" separator is found, we use this to set the attribute ordering
                // but no label is set.
                std::string folder = (uigroupSplit.size() > 1) ? uigroupSplit[0] : "";
                std::string uigroupAttrs  = uigroupSplit.back();

                // The list of attributes for a given group is separated by spaces
                const auto &uigroupAttrList = TfStringSplit(uigroupAttrs, " ");
                for (const auto& uigroupAttr : uigroupAttrList) {
                    if (uigroupAttr.empty())
                        continue;

                    // if this attribute was previously declared, skip it
                    if (declaredAttributes.find(uigroupAttr) != declaredAttributes.end())
                        continue;

                    // Add this attribute to the properties list
                    declaredAttributes.insert(uigroupAttr);
                    UsdAttribute attr = prim.GetAttribute(TfToken(uigroupAttr.c_str()));
                    if (attr) {
                        _ReadShaderAttribute(attr, properties, folder);
                    }
                }                
            }
        }
    }

    // For the attributes that were not explicitely organized through the "ui.groups" metadata,
    // we now create them in the same order as they appeared in the AtParamIterator in _ReadArnoldShaderDef
    if (primCustomData.find(_tokens->attrsOrder) != primCustomData.end()) {
        VtValue attrsOrderVal = primCustomData[_tokens->attrsOrder];
        const VtArray<std::string> &attrsOrder = attrsOrderVal.Get<VtArray<std::string>>();
        for (const auto &attrName: attrsOrder) {
            if (declaredAttributes.find(attrName) != declaredAttributes.end())
                continue;

            UsdAttribute attr = prim.GetAttribute(TfToken(attrName.c_str()));
            if (attr) {
                declaredAttributes.insert(attrName);
                _ReadShaderAttribute(attr, properties, "");
            }
        }
    }

    // Now loop over all usd properties that were declared, and add 
    // the ones that weren't added previously with ui.groups or attrsOrder.
    // Note that there shouldn't be any left attribute here since they should 
    // all appear in attrsOrder
    for (const UsdProperty& property : props) {
        const TfToken& propertyName = property.GetName();
        const std::string& propertyNameStr = propertyName.GetString();
        if (declaredAttributes.find(propertyNameStr) != declaredAttributes.end())
            continue;
        
        const auto propertyStack = property.GetPropertyStack();
        if (propertyStack.empty()) {
            continue;
        }

        const auto attr = prim.GetAttribute(propertyName);
        // If this attribute was already declared in the above code 
        // for ui.groups, we don't want to declare it again
        
        declaredAttributes.insert(propertyNameStr);
        _ReadShaderAttribute(attr, properties, "");
    }

    // Now handle the metadatas at the node level
    NdrTokenMap metadata;
    for (const auto &it : primCustomData) {
        // uigroups was handled above
        if (it.first == _tokens->uigroups || it.first == _tokens->attrsOrder)
            continue;
        metadata.insert({TfToken(it.first), TfStringify(it.second)});
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
        std::move(properties),
        metadata));
}

const NdrTokenVec& NdrArnoldParserPlugin::GetDiscoveryTypes() const
{
    static const NdrTokenVec ret = {_tokens->arnold};
    return ret;
}

const TfToken& NdrArnoldParserPlugin::GetSourceType() const { return _tokens->arnold; }

PXR_NAMESPACE_CLOSE_SCOPE
