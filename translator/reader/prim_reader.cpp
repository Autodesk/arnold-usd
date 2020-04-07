// Copyright 2019 Autodesk, Inc.
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
#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/base/tf/token.h>
#include "prim_reader.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// Unsupported node types should dump a warning when being converted
void UsdArnoldReadUnsupported::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AiMsgWarning(
        "UsdArnoldReader : %s primitives not supported, cannot read %s", _type.c_str(), prim.GetName().GetText());
}

/**
 *   Read all the arnold-specific attributes that were saved in this USD
 *primitive. Arnold attributes are prefixed with the namespace 'arnold:' We will
 *strip this prefix, look for the corresponding arnold parameter, and convert it
 *based on its type
 **/
void UsdArnoldPrimReader::readArnoldParameters(
    const UsdPrim &prim, UsdArnoldReaderContext &context, AtNode *node, const TimeSettings &time, const std::string &scope)
{
    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
    if (nodeEntry == NULL) {
        return; // shouldn't happen
    }

    float frame = time.frame;
    UsdAttributeVector attributes = prim.GetAttributes();

    // We currently support the following namespaces for arnold input attributes
    TfToken scopeToken(scope);

    for (size_t i = 0; i < attributes.size(); ++i) {
        UsdAttribute &attr = attributes[i];
        TfToken attrNamespace = attr.GetNamespace();

        if (attrNamespace != scopeToken) { // only deal with attributes of the desired scope
            continue;
        }

        std::string arnoldAttr = attr.GetBaseName().GetString();
        const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(arnoldAttr.c_str()));
        if (paramEntry == NULL) {
            AiMsgWarning(
                "USD arnold attribute %s not recognized in %s", arnoldAttr.c_str(), AiNodeEntryGetName(nodeEntry));
            continue;
        }
        uint8_t paramType = AiParamGetType(paramEntry);

        VtValue vtValue;
        attr.Get(&vtValue, frame);

        if (paramType == AI_TYPE_ARRAY) {
            // Let's separate the code for arrays as it's more complicated
            const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
            // Getting the default array, and checking its type
            int arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;

            if (arrayType == AI_TYPE_NONE) {
                // No default value found in the AtParamEntry default
                // definition, let's check the USD value instead
                if (vtValue.IsHolding<VtArray<unsigned char> >())
                    arrayType = AI_TYPE_BYTE;
                else if (vtValue.IsHolding<VtArray<int> >())
                    arrayType = AI_TYPE_INT;
                else if (vtValue.IsHolding<VtArray<unsigned int> >())
                    arrayType = AI_TYPE_UINT;
                else if (vtValue.IsHolding<VtArray<bool> >())
                    arrayType = AI_TYPE_BOOLEAN;
                else if (vtValue.IsHolding<VtArray<float> >())
                    arrayType = AI_TYPE_FLOAT;
                else if (vtValue.IsHolding<VtArray<GfVec3f> >())
                    arrayType = AI_TYPE_VECTOR;
                else if (vtValue.IsHolding<VtArray<GfVec4f> >())
                    arrayType = AI_TYPE_RGBA;
                else if (vtValue.IsHolding<VtArray<GfVec2f> >())
                    arrayType = AI_TYPE_VECTOR2;
                else if (vtValue.IsHolding<VtArray<TfToken> >())
                    arrayType = AI_TYPE_STRING;
                else if (vtValue.IsHolding<VtArray<GfMatrix4d> >())
                    arrayType = AI_TYPE_MATRIX;
            }

            // The default array has a type, let's do the conversion based on it
            switch (arrayType) {
                case AI_TYPE_BYTE:
                    exportArray<unsigned char, unsigned char>(attr, node, arnoldAttr.c_str(), time);
                    break;
                case AI_TYPE_INT:
                    exportArray<int, int>(attr, node, arnoldAttr.c_str(), time);
                    break;
                case AI_TYPE_UINT:
                    exportArray<unsigned int, unsigned int>(attr, node, arnoldAttr.c_str(), time);
                    break;
                case AI_TYPE_BOOLEAN:
                    exportArray<bool, bool>(attr, node, arnoldAttr.c_str(), time);
                    break;
                case AI_TYPE_FLOAT:
                    exportArray<float, float>(attr, node, arnoldAttr.c_str(), time);
                    break;
                case AI_TYPE_VECTOR:
                case AI_TYPE_RGB:
                    // Vector and RGB are represented as GfVec3f, so we need to pass the type
                    // (AI_TYPE_VECTOR / AI_TYPE_RGB) so that the arnold array is set properly #325
                    exportArray<GfVec3f, GfVec3f>(attr, node, arnoldAttr.c_str(), time, arrayType);
                    break;
                case AI_TYPE_RGBA:
                    exportArray<GfVec4f, GfVec4f>(attr, node, arnoldAttr.c_str(), time);
                    break;
                case AI_TYPE_VECTOR2:
                    exportArray<GfVec2f, GfVec2f>(attr, node, arnoldAttr.c_str(), time);
                    break;
                case AI_TYPE_ENUM:
                case AI_TYPE_STRING:
                    exportStringArray(attr, node, arnoldAttr.c_str(), time);
                    break;
                case AI_TYPE_MATRIX:
                    exportArray<GfMatrix4d, GfMatrix4d>(attr, node, arnoldAttr.c_str(), time);
                    break;
                {
                case AI_TYPE_NODE:
                    std::string serializedArray;                    
                    VtArray<std::string> array;
                    attr.Get(&array, frame);
                    for (size_t v = 0; v < array.size(); ++v) {
                        std::string nodeName = array[v];
                        if (nodeName.empty()) {
                            continue;
                        }
                        if (nodeName[0] != '/')
                            nodeName = std::string("/") + nodeName;

                        if (!serializedArray.empty())
                            serializedArray += std::string(" ");
                        serializedArray += nodeName;
                    }
                    context.addConnection(node, arnoldAttr, serializedArray, UsdArnoldReaderContext::CONNECTION_ARRAY);
                    break;
                        
                }
                default:
                    break;
            }
        } else {
            // Simple parameters (not-an-array)
            switch (paramType) {
                case AI_TYPE_BYTE:
                    AiNodeSetByte(node, arnoldAttr.c_str(), vtValueGetByte(vtValue));
                    break;
                case AI_TYPE_INT:
                    AiNodeSetInt(node, arnoldAttr.c_str(), vtValueGetInt(vtValue));
                    break;
                case AI_TYPE_UINT:
                case AI_TYPE_USHORT:
                    AiNodeSetUInt(node, arnoldAttr.c_str(), vtValue.Get<unsigned int>());
                    break;
                case AI_TYPE_BOOLEAN:
                    AiNodeSetBool(node, arnoldAttr.c_str(), vtValueGetBool(vtValue));
                    break;
                case AI_TYPE_FLOAT:
                case AI_TYPE_HALF:
                    AiNodeSetFlt(node, arnoldAttr.c_str(), vtValueGetFloat(vtValue));
                    break;
                {
                case AI_TYPE_VECTOR:
                    GfVec3f vec = vtValue.Get<GfVec3f>();
                    AiNodeSetVec(node, arnoldAttr.c_str(), vec[0], vec[1], vec[2]);
                    break;
                }
                {
                case AI_TYPE_RGB:
                    GfVec3f vec = vtValue.Get<GfVec3f>();
                    AiNodeSetRGB(node, arnoldAttr.c_str(), vec[0], vec[1], vec[2]);
                    break;
                }
                {
                case AI_TYPE_RGBA:
                    GfVec4f vec = vtValue.Get<GfVec4f>();
                    AiNodeSetRGBA(node, arnoldAttr.c_str(), vec[0], vec[1], vec[2], vec[3]);
                    break;
                }
                {
                case AI_TYPE_VECTOR2:
                    GfVec2f vec = vtValue.Get<GfVec2f>();
                    AiNodeSetVec2(node, arnoldAttr.c_str(), vec[0], vec[1]);
                    break;
                }
                case AI_TYPE_ENUM:
                    if (vtValue.IsHolding<int>()) {
                        AiNodeSetInt(node, arnoldAttr.c_str(), vtValue.UncheckedGet<int>());
                        break;
                    } else if (vtValue.IsHolding<long>()) {
                        AiNodeSetInt(node, arnoldAttr.c_str(), vtValue.UncheckedGet<long>());
                        break;
                    } 
                {
                case AI_TYPE_STRING:
                    if (vtValue.IsHolding<std::string>()) {
                        auto str = vtValue.UncheckedGet<std::string>();
                        AiNodeSetStr(node, arnoldAttr.c_str(), str.c_str());
                    } else if (vtValue.IsHolding<TfToken>()) {
                        auto token = vtValue.UncheckedGet<TfToken>();
                        AiNodeSetStr(node, arnoldAttr.c_str(), token.GetText());
                    } else if (vtValue.IsHolding<SdfAssetPath>()) {
                        auto assetPath = vtValue.UncheckedGet<SdfAssetPath>();
                        auto path = assetPath.GetResolvedPath();
                        if (path.empty()) {
                            path = assetPath.GetAssetPath();
                        }
                        AiNodeSetStr(node, arnoldAttr.c_str(), path.c_str());
                    }
                    break;
                }
                {
                case AI_TYPE_MATRIX:
                    GfMatrix4d usdMat = vtValue.Get<GfMatrix4d>();
                    AtMatrix aiMat;
                    const double *array = usdMat.GetArray();
                    for (unsigned int i = 0; i < 4; ++i)
                        for (unsigned int j = 0; j < 4; ++j)
                            aiMat[i][j] = array[4 * i + j];
                    AiNodeSetMatrix(node, arnoldAttr.c_str(), aiMat);
                    break;
                }
                {
                // node attributes are expected as strings
                case AI_TYPE_NODE:
                    std::string nodeName = vtValue.Get<std::string>();
                    if (!nodeName.empty()) {
                        if (nodeName[0] != '/')
                            nodeName = std::string("/") + nodeName;
                        context.addConnection(node, arnoldAttr, nodeName, UsdArnoldReaderContext::CONNECTION_PTR);
                    }
                    break;
                }
                default:
                    break;
            }
            // check if there are connections to this attribute
            if (attr.HasAuthoredConnections()) {
                SdfPathVector targets;
                attr.GetConnections(&targets);
                // arnold can only have a single connection to an attribute
                if (!targets.empty()) {
                    SdfPath &target = targets[0]; // just consider the first target
                    UsdArnoldReaderContext::ConnectionType conn = UsdArnoldReaderContext::CONNECTION_LINK;
                    std::string elem = target.GetElementString(); // this will return i.e. ".outputs:rgb"
                    if (!target.IsPrimPath() && elem.length() > 1 && elem[elem.length() - 2] == ':') {
                        char elemChar = elem.back(); // the last character in the string is the component
                        // Set the link type accordingly
                        if (elemChar == 'x')
                            conn = UsdArnoldReaderContext::CONNECTION_LINK_X;
                        else if (elemChar == 'y')
                            conn = UsdArnoldReaderContext::CONNECTION_LINK_Y;
                        else if (elemChar == 'z')
                            conn = UsdArnoldReaderContext::CONNECTION_LINK_Z;
                        else if (elemChar == 'r')
                            conn = UsdArnoldReaderContext::CONNECTION_LINK_R;
                        else if (elemChar == 'g')
                            conn = UsdArnoldReaderContext::CONNECTION_LINK_G;
                        else if (elemChar == 'b')
                            conn = UsdArnoldReaderContext::CONNECTION_LINK_B;
                        else if (elemChar == 'a')
                            conn = UsdArnoldReaderContext::CONNECTION_LINK_A;
                    }
                    context.addConnection(node, arnoldAttr, targets[0].GetPrimPath().GetText(), conn);
                }
            }
        }
    }
}
