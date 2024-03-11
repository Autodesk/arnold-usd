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
#include "read_shader.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/nodeGraph.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/utils.h>
#include <pxr/base/gf/rotation.h>

#include <common_utils.h>
#include <constant_strings.h>
#include <parameters_utils.h>
#include <materials_utils.h>

#include "registry.h"
#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

class MaterialUsdReader : public MaterialReader
{
public:
    MaterialUsdReader(UsdArnoldPrimReader& shaderReader, UsdArnoldReaderContext& context) : 
        _shaderReader(shaderReader),
        _context(context),
        _reader(context.GetReader()),
        MaterialReader() {}

    AtNode* CreateArnoldNode(const char* nodeType, const char* nodeName) override
    {
        return _context.CreateArnoldNode(nodeType, nodeName);
    }
    void ConnectShader(AtNode* node, const std::string& attrName, 
            const SdfPath& target) override {
        
        SdfPath targetPrimPath(target.GetPrimPath());
        _context.AddConnection(
            node, attrName.c_str(), targetPrimPath.GetText(),
            ArnoldAPIAdapter::CONNECTION_LINK, target.GetElementString());

#ifdef ARNOLD_USD_MATERIAL_READER
        UsdPrim targetPrim = 
            _reader->GetStage()->GetPrimAtPath(target.GetPrimPath());

        if (!targetPrim)
            return;

        UsdArnoldPrimReader* primReader = 
            (targetPrim.IsA<UsdShadeShader>()) ? 
            &_shaderReader : 
            _reader->GetRegistry()->GetPrimReader(targetPrim.GetTypeName().GetString());

        AtNode* targetNode = nullptr;
        if (primReader) {
            // FIXME instead call directly another read call ?
            targetNode = primReader->Read(targetPrim, static_cast<UsdArnoldReaderContext&>(_context));
        }
#endif
    }

    bool GetShaderInput(const SdfPath& shaderPath, const TfToken& param, 
        VtValue& value, TfToken& shaderId) override 
    {
        UsdPrim prim = 
            _reader->GetStage()->GetPrimAtPath(shaderPath);

        if (!prim || !prim.IsA<UsdShadeShader>())
            return false;

        UsdShadeShader shader(prim);
        shader.GetIdAttr().Get(&shaderId);

        UsdShadeInput input = shader.GetInput(param);
        if (!input)
            return false;


#if PXR_VERSION > 2011
        const UsdShadeAttributeVector attrs = 
            UsdShadeUtils::GetValueProducingAttributes(input);

        if (attrs.empty())
            return false;

        return attrs[0].Get(&value);
#else
        return input.Get(&value);
#endif
        
    }
private:
    UsdArnoldPrimReader& _shaderReader;
    UsdArnoldReaderContext& _context;
    UsdArnoldReader *_reader = nullptr;
};

AtNode* UsdArnoldReadNodeGraph::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{    
    if (prim.IsA<UsdShadeMaterial>()) {
        UsdShadeMaterial mat(prim);
        if (!mat)
            return nullptr;
        AtNode* shader = nullptr;
        UsdPrim shaderPrim, dispPrim;
        GetMaterialTargets(mat, shaderPrim, &dispPrim);
        if (shaderPrim && shaderPrim.GetPath().HasPrefix(prim.GetPath())) {
            shader = _shaderReader.Read(shaderPrim, context);
        }
        if (dispPrim && dispPrim.GetPath().HasPrefix(prim.GetPath())) {
            _shaderReader.Read(dispPrim, context);
        }
        return shader;
    } 
    UsdShadeNodeGraph nodeGraph(prim);
    if (!nodeGraph)
        return nullptr;

    std::vector<UsdShadeOutput> outputs = nodeGraph.GetOutputs();
    if (outputs.empty())
        return nullptr;

    AtNode *node = nullptr;
    for (const auto& output : outputs) {
        UsdPrim outputPrim = output.GetPrim();
        if (outputPrim) {
            AtNode *shader = _shaderReader.Read(outputPrim, context);
            if (node == nullptr)
                node = shader;
        }
    }
    return node;
}

/** Read USD native shaders to Arnold
 *
 **/
AtNode* UsdArnoldReadShader::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{

    const std::string& nodeName = prim.GetPath().GetString();
    // Ensure we don't re-export a shader that was already exported 
    // (which can happen when a shader is connected multiple times).
    // However, if we're doing an interactive update, we cannot skip this.
    if (!context.GetReader()->IsUpdating()) {
        AtNode* node = context.GetReader()->LookupNode(nodeName.c_str());
        if (node) {
            return node;
        }
    }

    UsdShadeShader shader(prim);
    const TimeSettings &time = context.GetTimeSettings();
    // The "Shader Id" will tell us what is the type of the shader
    TfToken id;
    shader.GetIdAttr().Get(&id, time.frame);
    if (id.IsEmpty())
        return nullptr;

    bool isArnoldShader = (TfStringStartsWith(id.GetString(), str::t_arnold_prefix.GetString()));
    const AtNodeEntry* nentry = isArnoldShader ? AiNodeEntryLookUp(id.GetString().substr(7).c_str()) : nullptr;

    const std::vector<UsdShadeInput> shadeNodeInputs = shader.GetInputs();
    InputAttributesList inputAttrs;
    int index = 0;
    if (!shadeNodeInputs.empty()) {
        inputAttrs.clear();
        inputAttrs.reserve(shadeNodeInputs.size());
        for (const UsdShadeInput& input : shadeNodeInputs) {

            UsdAttribute attr = input.GetAttr();
            bool hasConnection = attr.HasAuthoredConnections();

            bool overrideConnection = false;
            SdfPath connection;

            if(hasConnection) {
#if PXR_VERSION > 2011
                // Find the attributes this input is getting its value from, which might
                // be an output or an input, including possibly itself if not connected
                const UsdShadeAttributeVector attrs = 
                    UsdShadeUtils::GetValueProducingAttributes(input);

                if (!attrs.empty()) {
                    if (attrs[0].HasAuthoredConnections() || 
                        UsdShadeUtils::GetType(attrs[0].GetName()) == UsdShadeAttributeType::Input) {
                        attr = attrs[0];
                    } else {
                        connection = attrs[0].GetPath();
                        overrideConnection = true;
                    }
                }
#else
                // Older USD versions, we check explicitely if we have a connection to a 
                // UsdShadeNodeGraph primitive, and consider this attribute instead
                SdfPathVector connections;
                attr.GetConnections(&connections);
                if (!connections.empty() && !connections[0].IsPrimPath()) {
                    SdfPath primPath = connections[0].GetPrimPath();
                    UsdPrim targetPrim = attr.GetPrim().GetStage()->GetPrimAtPath(primPath);
                    if (targetPrim && targetPrim.IsA<UsdShadeNodeGraph>()) {
                        std::string outputElement = connections[0].GetElementString();
                        if (!outputElement.empty() && outputElement[0] == '.')
                            outputElement = outputElement.substr(1);
                        
                        UsdAttribute nodeGraphAttr = targetPrim.GetAttribute(TfToken(outputElement));
                        if (nodeGraphAttr) {
                            attr = nodeGraphAttr;
                        }
                    }
                }
#endif
            }
            TfToken attrName = input.GetBaseName();
            InputAttribute& inputAttr = inputAttrs[attrName];
            int paramType = AI_TYPE_NONE;
            int arrayType = AI_TYPE_NONE;
            if (nentry) {
                const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nentry, AtString(attrName.GetText()));
                paramType = AiParamGetType(paramEntry);
                if (paramType == AI_TYPE_ARRAY) {
                    const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
                    // Getting the default array, and checking its type
                    arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;
                }
            }
            
            CreateInputAttribute(inputAttr, attr, context.GetTimeSettings(), paramType, arrayType);
            
            if (overrideConnection)
                inputAttr.connection = connection;

            if (TfStringStartsWith(attrName, "file") && 
                    attr.HasMetadata(str::t_colorSpace)) {
                // if a metadata is present, set this value in the OSL shader.
                // For now this is only needed for OSL shader file attributes
                VtValue colorSpaceValue;
                attr.GetMetadata(str::t_colorSpace, &colorSpaceValue);
                if (!colorSpaceValue.IsEmpty()) {
                    std::string colorSpaceStr = attrName.GetString() + ":colorSpace";
                    TfToken colorSpace(colorSpaceStr);
                    inputAttrs[colorSpace].value = colorSpaceValue;
                }
            }
        }
    }
    MaterialUsdReader materialReader(*this, context);
    return ReadShader(nodeName, id, inputAttrs, context, time, materialReader);
}
                            
void UsdArnoldReadShader::_ReadShaderParameter(
    UsdShadeShader &shader, AtNode *node, const std::string &usdAttr, const std::string &arnoldAttr,
    UsdArnoldReaderContext &context)
{
    if (node == nullptr)
        return;

    UsdShadeInput paramInput = shader.GetInput(TfToken(usdAttr.c_str()));
    if (!paramInput)
        return;

    _ReadShaderInput(paramInput, node, arnoldAttr, context);
}
void UsdArnoldReadShader::_ReadShaderInput(const UsdShadeInput& input, AtNode* node, 
    const std::string& arnoldAttr, UsdArnoldReaderContext& context)
{
    const AtNodeEntry *nentry = node ? AiNodeGetNodeEntry(node) : nullptr;
    if (nentry == nullptr) {
        return;
    }
    UsdAttribute attr = input.GetAttr();
    bool hasConnection = attr.HasAuthoredConnections();
    
    if(hasConnection) {
        TfToken inputName = input.GetBaseName();
        if (inputName != attr.GetBaseName()) {
            // Linked array attributes : This isn't supported natively in USD, so we need
            // to read it in a specific format. If attribute "attr" has element 1 linked to
            // a shader, we will write it as attr:i1
            size_t indexPos = inputName.GetString().find(":i");
            if (indexPos != std::string::npos) {
                UsdPrim prim = attr.GetPrim();
                ReadArrayLink(prim, attr, context.GetTimeSettings(), context, node, str::t_inputs);
                return;
            }
        }
    }
    
    const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nentry, AtString(arnoldAttr.c_str()));
    int paramType = AiParamGetType(paramEntry);

    if (paramEntry == nullptr) {
        TfToken inputName = input.GetFullName();
        AiMsgWarning(
            "USD arnold attribute %s not recognized in %s for %s", 
            inputName.GetText(), AiNodeEntryGetName(nentry), AiNodeGetName(node));
        return;
    }
    bool overrideConnection = false;
    SdfPath connection;

    if(hasConnection) {
#if PXR_VERSION > 2011
        // Find the attributes this input is getting its value from, which might
        // be an output or an input, including possibly itself if not connected
        const UsdShadeAttributeVector attrs = 
            UsdShadeUtils::GetValueProducingAttributes(input);

        if (!attrs.empty()) {
            if (attrs[0].HasAuthoredConnections() || 
                UsdShadeUtils::GetType(attrs[0].GetName()) == UsdShadeAttributeType::Input) {
                attr = attrs[0];
            } else {
                connection = attrs[0].GetPath();
                overrideConnection = true;
            }
        }
#else
        // Older USD versions, we check explicitely if we have a connection to a 
        // UsdShadeNodeGraph primitive, and consider this attribute instead
        SdfPathVector connections;
        attr.GetConnections(&connections);
        if (!connections.empty() && !connections[0].IsPrimPath()) {
            SdfPath primPath = connections[0].GetPrimPath();
            UsdPrim targetPrim = attr.GetPrim().GetStage()->GetPrimAtPath(primPath);
            if (targetPrim && targetPrim.IsA<UsdShadeNodeGraph>()) {
                std::string outputElement = connections[0].GetElementString();
                if (!outputElement.empty() && outputElement[0] == '.')
                    outputElement = outputElement.substr(1);
                
                UsdAttribute nodeGraphAttr = targetPrim.GetAttribute(TfToken(outputElement));
                if (nodeGraphAttr) {
                    attr = nodeGraphAttr;
                }
            }
        }
#endif
    }

    int arrayType = AI_TYPE_NONE;
    if (paramType == AI_TYPE_ARRAY) {
        const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
        // Getting the default array, and checking its type
        arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;
    }
    InputAttribute inputAttr;
    CreateInputAttribute(inputAttr, attr, context.GetTimeSettings(), paramType, arrayType);
    if (overrideConnection)
        inputAttr.connection = connection;

    ReadAttribute(inputAttr, node, arnoldAttr, context.GetTimeSettings(), context, paramType, arrayType);
}

void UsdArnoldReadShader::ReadShaderInputs(const UsdPrim &prim, UsdArnoldReaderContext &context, 
    AtNode* node)
{
    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
    if (nodeEntry == nullptr) {
        return; // shouldn't happen
    }
    UsdShadeShader shader(prim);
    const TimeSettings& time = context.GetTimeSettings();

    // For OSL shaders, we first need to read the "code" attribute and set it, 
    // as it will change the AtNodeEntry
    bool isOsl = AiNodeIs(node, str::osl);
    if (isOsl) {
        UsdAttribute oslCode = prim.GetAttribute(str::t_inputs_code);
        VtValue value;
        if (oslCode && oslCode.Get(&value, time.frame)) {
            std::string code = VtValueGetString(value);
            if (!code.empty()) {
                AiNodeSetStr(node, str::code, AtString(code.c_str()));
                // Need to update the node entry that was
                // modified after "code" is set
                nodeEntry = AiNodeGetNodeEntry(node);
            }
        }
    }

    // Visit the inputs of this node to ensure they are emitted first.
    const std::vector<UsdShadeInput> shadeNodeInputs = shader.GetInputs();
    for (const UsdShadeInput& input : shadeNodeInputs) {

        TfToken inputName = input.GetBaseName();
        // osl "code" attribute was already handled previously,
        // we can skip it here
        if (isOsl && inputName == str::t_code)
            continue;

        if (inputName == str::t_name) {
            // If attribute "name" is set in the usd prim, we need to set the node name
            // accordingly. We also store this node original name in a map, that we
            // might use later on, when processing connections.
            VtValue nameValue;
            if (input.GetAttr().Get(&nameValue, time.frame)) {
                std::string nameStr = VtValueGetString(nameValue);
                std::string usdName = prim.GetPath().GetText();
                if ((!nameStr.empty()) && nameStr != usdName) {
                    AiNodeSetStr(node, str::name, AtString(nameStr.c_str()));
                    context.AddNodeName(usdName, node);
                }
            }
            continue;
        }
        _ReadShaderInput(input, node, inputName.GetString(), context);
    }
}
        
