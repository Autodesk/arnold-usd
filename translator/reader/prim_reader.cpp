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
                    {
                        case AI_TYPE_VECTOR:
                        case AI_TYPE_RGB:
                            exportArray<GfVec3f, GfVec3f>(attr, node, arnoldAttr.c_str(), time);
                            break;
                    }
                    {
                        case AI_TYPE_RGBA:
                            exportArray<GfVec4f, GfVec4f>(attr, node, arnoldAttr.c_str(), time);
                            break;
                    }
                    {
                        case AI_TYPE_VECTOR2:
                            exportArray<GfVec2f, GfVec2f>(attr, node, arnoldAttr.c_str(), time);
                            break;
                    }
                    {
                            // FIXME: ensure enum is working here
                        case AI_TYPE_ENUM:
                        case AI_TYPE_STRING:
                            exportArray<TfToken, TfToken>(attr, node, arnoldAttr.c_str(), time);
                            break;
                    }
                    {
                        case AI_TYPE_MATRIX:
                            VtArray<GfMatrix4d> array;
                            if (!attr.Get(&array, frame) || array.empty()) {
                                continue;
                            }
                            // special case for matrices. They're single
                            // precision in arnold but double precision in USD,
                            // and there is no copy from one to the other.
                            std::vector<AtMatrix> arnoldVec(array.size());
                            for (size_t v = 0; v < arnoldVec.size(); ++v) {
                                AtMatrix &aiMat = arnoldVec[v];
                                const double *matArray = array[v].GetArray();
                                for (unsigned int i = 0; i < 4; ++i)
                                    for (unsigned int j = 0; j < 4; ++j)
                                        aiMat[i][j] = matArray[4 * i + j];
                            }
                            AiNodeSetArray(
                                node, arnoldAttr.c_str(),
                                AiArrayConvert(array.size(), 1, AI_TYPE_MATRIX, &arnoldVec[0]));
                            break;
                    }
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
                    AiNodeSetByte(node, arnoldAttr.c_str(), vtValue.Get<unsigned char>());
                    break;
                case AI_TYPE_INT:
                    AiNodeSetInt(node, arnoldAttr.c_str(), vtValue.Get<int>());
                    break;
                case AI_TYPE_UINT:
                    AiNodeSetUInt(node, arnoldAttr.c_str(), vtValue.Get<unsigned int>());
                    break;
                case AI_TYPE_BOOLEAN:
                    AiNodeSetBool(node, arnoldAttr.c_str(), vtValue.Get<bool>());
                    break;
                case AI_TYPE_FLOAT:
                    AiNodeSetFlt(node, arnoldAttr.c_str(), vtValue.Get<float>());
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
                    {
                            // FIXME: ensure enum is working here
                        case AI_TYPE_ENUM:
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
                    context.addConnection(node, arnoldAttr, targets[0].GetText(), UsdArnoldReaderContext::CONNECTION_LINK);
                }
            }
        }
    }
}
