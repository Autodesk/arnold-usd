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
#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <numeric>
#include <pxr/usd/usd/primCompositionQuery.h>

#include <pxr/usd/pcp/layerStack.h>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

#include <constant_strings.h>
#include "parameters_utils.h"
#include <common_utils.h>
#include "api_adapter.h"
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE


// Node & node array attributes are saved as strings, pointing to the arnold node name.
// But if this usd file is referenced from another one, it will automatically be added a prefix by USD
// composition arcs, and thus we won't be able to find the proper arnold node name based on its name.
// ValidatePrimPath handles this, by eventually adjusting the prim path 

bool _ReadArrayAttribute(const InputAttribute& attr, AtNode* node, const char* attrName, const TimeSettings& time, 
    ArnoldAPIAdapter &context, uint8_t arrayType = AI_TYPE_NONE)
{
    if (arrayType == AI_TYPE_NODE) {
        if (attr.value.IsEmpty()) {
            return false;
        }
        
        if (attr.value.IsHolding<VtArray<std::string>>()) {
            std::string serializedArray;
            const VtArray<std::string> &array = attr.value.UncheckedGet<VtArray<std::string>>();
            for (const auto& nodeName : array) {
                if (nodeName.empty()) {
                    continue;
                }
                if (!serializedArray.empty())
                    serializedArray += std::string(" ");

                serializedArray += nodeName;
            }
            context.AddConnection(node, attrName, serializedArray, ArnoldAPIAdapter::CONNECTION_ARRAY);
            return true;
        }
        return false;
    }

    if (attr.timeValues == nullptr || attr.timeValues->empty()) {
        if (attr.value.IsEmpty()) {
            AiNodeResetParameter(node, AtString(attrName));
            return false;
        }
        // Single-key arrays
        std::vector<VtValue> values;
        values.push_back(attr.value);

        AtArray *array = VtValueGetArray(values, arrayType, context);
        if (array == nullptr) {
            AiNodeResetParameter(node, AtString(attrName));
            return false;
        }

        AiNodeSetArray(node, AtString(attrName), array);
        return true;
    }

    AtArray *array = VtValueGetArray(*attr.timeValues, arrayType, context);
    if (array == nullptr) {
        AiNodeResetParameter(node, AtString(attrName));
        return false;
    }
    
    AiNodeSetArray(node, AtString(attrName), array);
    return true;


}

void _ReadAttributeConnection(
    const SdfPath &connection, AtNode *node, 
    const std::string &arnoldAttr,  const TimeSettings &time, 
    ArnoldAPIAdapter &context, int paramType)
{
    if (connection.IsEmpty())
        return;
    
    SdfPath primPath = connection.GetPrimPath();
    std::string outputElement;
    if (!connection.IsPrimPath()) {
        outputElement = connection.GetElementString();
        if (!outputElement.empty() && outputElement[0] == '.')
            outputElement = outputElement.substr(1);
    }

    // if it's an imager then use a CONNECTION_PTR

    context.AddConnection(node, arnoldAttr, connection.GetPrimPath().GetText(),
                          AiNodeEntryGetType(AiNodeGetNodeEntry(node)) == AI_NODE_IMAGER ?
                          ArnoldAPIAdapter::CONNECTION_PTR : ArnoldAPIAdapter::CONNECTION_LINK,
                          outputElement);

}


bool _ValidatePrimPath(std::string& path, const UsdPrim& prim)
{
    SdfPath sdfPath(path.c_str());
    UsdPrim targetPrim = prim.GetStage()->GetPrimAtPath(sdfPath);
    // the prim path already exists, nothing to do
    if (targetPrim)
        return false;

    // At this point the primitive couldn't be found, let's check the composition arcs and see
    // if this primitive has an additional scope    
    UsdPrimCompositionQuery compQuery(prim);
    std::vector<UsdPrimCompositionQueryArc> compArcs = compQuery.GetCompositionArcs();
    for (auto &compArc : compArcs) {
        const std::string &introducingPrimPath = compArc.GetIntroducingPrimPath().GetText();
        if (introducingPrimPath.empty())
            continue;
                                    
        PcpNodeRef nodeRef = compArc.GetTargetNode();
        PcpLayerStackRefPtr stackRef = nodeRef.GetLayerStack();
        const auto &layers = stackRef->GetLayers();
        for (const auto &layer : layers) {
            // We need to remove the defaultPrim path from the primitive name, and then
            // prefix it with the introducing prim path. This will return the actual primitive
            // name in the current usd stage
            std::string defaultPrimName = std::string("/") + layer->GetDefaultPrim().GetString();
            if (defaultPrimName.length() < path.length() && 
                defaultPrimName == path.substr(0, defaultPrimName.length())) {
                std::string composedName = introducingPrimPath + path.substr(defaultPrimName.length());
                sdfPath = SdfPath(composedName);
                // We found a primitive with this new path, we can override the path
                if (prim.GetStage()->GetPrimAtPath(sdfPath))
                {
                    path = composedName;
                    return true;
                }       
            }
        }
    }
    return false;
}
void ReadAttribute(const UsdAttribute &attr, AtNode *node, const std::string &arnoldAttr, const TimeSettings &time,
                ArnoldAPIAdapter &context, int paramType, int arrayType)
{
    InputAttribute inputAttr;
    CreateInputAttribute(inputAttr, attr, time, paramType, arrayType);
    ReadAttribute(inputAttr, node, arnoldAttr, time, context, paramType, arrayType);
}

void CreateInputAttribute(InputAttribute& inputAttr, const UsdAttribute& attr, const TimeSettings& time,
            int paramType, int arrayType, ValueReader* valueReader)
{
    bool motionBlur = time.motionBlur && paramType == AI_TYPE_ARRAY && 
        arrayType != AI_TYPE_NODE && attr.ValueMightBeTimeVarying();
    
    if (motionBlur) {
        GfInterval interval(time.start(), time.end(), false, false);
        std::vector<double> timeSamples;
        attr.GetTimeSamplesInInterval(interval, &timeSamples);
        // need to add the start end end keys (interval has open bounds)
        size_t numKeys = timeSamples.size() + 2;

        double timeStep = double(interval.GetMax() - interval.GetMin()) / double(numKeys - 1);
        double timeMin = interval.GetMin();
        double timeVal = timeMin;
        size_t numElements = 0;

        inputAttr.timeValues = new std::vector<VtValue>(numKeys);
        for (VtValue& value : (*inputAttr.timeValues)) {
            // loop through each time key. If we can't get the VtValue for this time
            // or if we find a varying amount of elements per key (not supported in Arnold)
            // then we'll switch to a single-time value
            bool hasValue = valueReader ? 
                valueReader->Get(&value, timeVal) : 
                attr.Get(&value, timeVal);
                
            if (!hasValue || 
                (timeVal > timeMin && value.GetArraySize() != numElements)) {

                motionBlur = false;
                delete inputAttr.timeValues;
                inputAttr.timeValues = nullptr;
                break;
            }
            numElements = value.GetArraySize();
            timeVal += timeStep;
        }
    }
    if (attr.HasAuthoredConnections()) {
        SdfPathVector connections;
        if (attr.GetConnections(&connections) && !connections.empty())
            inputAttr.connection = connections[0];
    }

    if (!motionBlur) {
        bool hasValue = valueReader ? 
            valueReader->Get(&inputAttr.value, time.frame) : 
            attr.Get(&inputAttr.value, time.frame);
        if (hasValue) {
            // NODE attributes are set as strings, but need to be remapped to  
            // actual node names
            if (paramType == AI_TYPE_NODE || arrayType == AI_TYPE_NODE) {
                if (inputAttr.value.IsHolding<std::string>()) {
                    std::string valueStr = inputAttr.value.UncheckedGet<std::string>();
                    if (_ValidatePrimPath(valueStr, attr.GetPrim())) {
                        inputAttr.value = VtValue::Take(valueStr);
                    }
                } else if (inputAttr.value.IsHolding<VtArray<std::string>>()) {
                    VtArray<std::string> valuesStr = inputAttr.value.UncheckedGet<VtArray<std::string>>();
                    bool changed = false;
                    for (auto& valueStr : valuesStr) {
                        changed |= _ValidatePrimPath(valueStr, attr.GetPrim());
                    }
                    if (changed) {
                        inputAttr.value = VtValue::Take(valuesStr);
                    }
                }
            }
            if (inputAttr.value.IsHolding<SdfAssetPath>()) {
                // Special treatment for asset attributes, which need to be 
                // read in a specific way as they sometimes cannot be resolved 
                // properly by USD, e.g. with UDIMS and relative paths (see #1129 and #1097)
                SdfAssetPath assetPath = inputAttr.value.UncheckedGet<SdfAssetPath>();
                // First, let's ask USD to resolve the path. In general, this is 
                // what will be used.
                std::string filenameStr = assetPath.GetResolvedPath();
                if (filenameStr.empty()) {
                    // If USD didn't manage to resolve the path, we get the raw "asset" path.
                    // If it's a relative path, we'll try to resolve it manually
                    filenameStr = assetPath.GetAssetPath();
                    bool remapRelativePath = !filenameStr.empty() && TfIsRelativePath(filenameStr);

                    // Check if there is a metadata preventing from remapping relative paths.
                    // When arnold image nodes are written to usd, this metadata will be present
                    // so that we always enforce the arnold way of handling relative paths (see #1878 and #1546)
                    if (attr.HasAuthoredCustomDataKey(str::t_arnold_relative_path)) {
                        remapRelativePath &= !VtValueGetBool(attr.GetCustomDataByKey(str::t_arnold_relative_path));
                    }
                    if (remapRelativePath) {
                        // SdfComputeAssetPathRelativeToLayer returns search paths (vs anchored paths) unmodified,
                        // this is apparently to make sure they will be always searched again.
                        // This is not what we want, so we make sure the path is anchored
                        if (filenameStr[0] != '.') {
                            filenameStr = "./" + filenameStr;
                        }
                        for (const auto& sdfProp : attr.GetPropertyStack()) {
                            const auto& layer = sdfProp->GetLayer();
                            if (layer && !layer->GetRealPath().empty()) {
                                std::string layerPath = SdfComputeAssetPathRelativeToLayer(layer, filenameStr);
                                if (!layerPath.empty() && layerPath != filenameStr &&
                                    TfPathExists(layerPath.substr(0, layerPath.find_last_of("\\/")))) {
                                    filenameStr = layerPath;
                                    break;
                                }
                            }
                        }
                    }
                }
                inputAttr.value = VtValue::Take(filenameStr);
            }
        }
    }    
}

void ReadAttribute(
    const InputAttribute &attr, AtNode *node, const std::string &arnoldAttr, 
    const TimeSettings &time, ArnoldAPIAdapter &context, int paramType, int arrayType)
{
    if (paramType == AI_TYPE_ARRAY) {
        _ReadArrayAttribute(attr, node, arnoldAttr.c_str(), time, context, arrayType);
    } else {
        const VtValue& value = attr.value;
        if (!value.IsEmpty()) {            
            // Simple parameters (not-an-array)
            switch (paramType) {
                case AI_TYPE_BYTE:
                    AiNodeSetByte(node, AtString(arnoldAttr.c_str()), VtValueGetByte(value));
                    break;
                case AI_TYPE_INT:
                    AiNodeSetInt(node, AtString(arnoldAttr.c_str()), VtValueGetInt(value));
                    break;
                case AI_TYPE_UINT:
                case AI_TYPE_USHORT:
                    AiNodeSetUInt(node, AtString(arnoldAttr.c_str()), VtValueGetUInt(value));
                    break;
                case AI_TYPE_BOOLEAN:
                    AiNodeSetBool(node, AtString(arnoldAttr.c_str()), VtValueGetBool(value));
                    break;
                case AI_TYPE_FLOAT:
                case AI_TYPE_HALF:
                    AiNodeSetFlt(node, AtString(arnoldAttr.c_str()), VtValueGetFloat(value));
                    break;

                case AI_TYPE_VECTOR: {
                    GfVec3f vec = VtValueGetVec3f(value);
                    AiNodeSetVec(node, AtString(arnoldAttr.c_str()), vec[0], vec[1], vec[2]);
                    break;
                }
                case AI_TYPE_RGB: {
                    GfVec3f vec = VtValueGetVec3f(value);
                    AiNodeSetRGB(node, AtString(arnoldAttr.c_str()), vec[0], vec[1], vec[2]);
                    break;
                }

                case AI_TYPE_RGBA: {
                    GfVec4f vec = VtValueGetVec4f(value);
                    AiNodeSetRGBA(node, AtString(arnoldAttr.c_str()), vec[0], vec[1], vec[2], vec[3]);
                    break;
                }
                case AI_TYPE_VECTOR2: {
                    GfVec2f vec = VtValueGetVec2f(value);
                    AiNodeSetVec2(node, AtString(arnoldAttr.c_str()), vec[0], vec[1]);
                    break;
                }
                case AI_TYPE_ENUM:
                    if (value.IsHolding<int>()) {
                        AiNodeSetInt(node, AtString(arnoldAttr.c_str()), value.UncheckedGet<int>());
                        break;
                    } else if (value.IsHolding<long>()) {
                        AiNodeSetInt(node, AtString(arnoldAttr.c_str()), value.UncheckedGet<long>());
                        break;
                    }
                // Enums can be strings, so we don't break here.
                case AI_TYPE_STRING: {
                    std::string str = VtValueGetString(value);
                    AiNodeSetStr(node, AtString(arnoldAttr.c_str()), AtString(str.c_str()));
                    break;
                }
                case AI_TYPE_MATRIX: {
                    AiNodeSetMatrix(node, AtString(arnoldAttr.c_str()), VtValueGetMatrix(value));
                    break;
                }
                // node attributes are expected as strings
                case AI_TYPE_NODE: {
                    std::string nodeName = VtValueGetString(value);
                    if (!nodeName.empty()) {
                        context.AddConnection(node, arnoldAttr, nodeName, ArnoldAPIAdapter::CONNECTION_PTR);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        // check if there are connections to this attribute
        bool isImager = AiNodeEntryGetType(AiNodeGetNodeEntry(node)) == AI_NODE_IMAGER;
        if ((paramType != AI_TYPE_NODE || isImager) && !attr.connection.IsEmpty())
            _ReadAttributeConnection(attr.connection, node, arnoldAttr, time, context, paramType);
    }
}


void ReadArrayLink(
    const UsdPrim &prim, const UsdAttribute &attr, const TimeSettings &time, 
    ArnoldAPIAdapter &context, AtNode *node, const std::string &scope)
{
    std::string attrNamespace = attr.GetNamespace().GetString();
    std::string indexStr = attr.GetBaseName().GetString();

    // We need at least 2 digits in the basename, for "i0", "i1", etc...
    if (indexStr.size() < 2 || indexStr[0] != 'i')
        return;

    // we're doing this only to handle connections, so if the attribute
    // isn't linked, we don't have anything to do here
    if (!attr.HasAuthoredConnections())
        return;

    indexStr = indexStr.substr(1); // remove the first "i" character

    // get the index
    int index = std::stoi(indexStr);
    if (index < 0)
        return;

    std::string attrName = (scope.empty()) ? attrNamespace : attrNamespace.substr(scope.length() + 1);

    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
    const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(attrName.c_str()));
    if (paramEntry == nullptr || AiParamGetType(paramEntry) != AI_TYPE_ARRAY)
        return;

    std::string attrElemName = attrName;
    attrElemName += "[";
    attrElemName += std::to_string(index);
    attrElemName += "]";

    SdfPathVector connections;
    attr.GetConnections(&connections);
    SdfPath connection;
    if (!connections.empty())
        connection = connections[0];
    _ReadAttributeConnection(connection, node, attrElemName, time, context, AI_TYPE_ARRAY);
}

inline uint8_t _GetRayFlag(uint8_t currentFlag, const std::string &rayName, const VtValue& value)
{
    auto flag = true;
    if (value.IsHolding<bool>()) {
        flag = value.UncheckedGet<bool>();
    } else if (value.IsHolding<int>()) {
        flag = value.UncheckedGet<int>() != 0;
    } else if (value.IsHolding<long>()) {
        flag = value.UncheckedGet<long>() != 0;
    } else {
        // Invalid value stored, just return the existing value.
        return currentFlag;
    }
    uint8_t bitFlag = 0;
    if (rayName == "camera")
        bitFlag = AI_RAY_CAMERA;
    else if (rayName == "shadow")
        bitFlag = AI_RAY_SHADOW;
    else if (rayName == "diffuse_transmit")
        bitFlag = AI_RAY_DIFFUSE_TRANSMIT;
    else if (rayName == "specular_transmit")
        bitFlag = AI_RAY_SPECULAR_TRANSMIT;
    else if (rayName == "volume")
        bitFlag = AI_RAY_VOLUME;
    else if (rayName ==  "diffuse_reflect")
        bitFlag = AI_RAY_DIFFUSE_REFLECT;
    else if (rayName == "specular_reflect")
        bitFlag = AI_RAY_SPECULAR_REFLECT;
    else if (rayName == "subsurface")
         bitFlag = AI_RAY_SUBSURFACE;

    return flag ? (currentFlag | bitFlag) : (currentFlag & ~bitFlag);
}

inline void _SetRayFlag(AtNode* node, const std::string& paramName, const std::string &rayName, const VtValue& value)
{
    AiNodeSetByte(node, AtString(paramName.c_str()), _GetRayFlag(AiNodeGetByte(node, AtString(paramName.c_str())), rayName, value));
}

/**
 *   Read all the arnold-specific attributes that were saved in this USD
 *primitive. Arnold attributes are prefixed with the namespace 'arnold:' We will
 *strip this prefix, look for the corresponding arnold parameter, and convert it
 *based on its type. 
 **/
void ReadArnoldParameters(
    const UsdPrim &prim, ArnoldAPIAdapter &context, AtNode *node, const TimeSettings &time,
    const std::string &scope)
{    
    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
    if (nodeEntry == nullptr) {
        return; // shouldn't happen
    }

    UsdAttributeVector attributes;
    // Check if the scope refers to primvars
    bool readPrimvars = (scope.length() >= 8 && scope.substr(0, 8) == "primvars");
    size_t attributeCount;

    // The reader context will return us the list of primvars for this primitive,
    // which was computed during the stage traversal, taking account the full
    // hierarchy.
    const std::vector<UsdGeomPrimvar> &primvars = context.GetPrimvars();

    if (readPrimvars) {
        attributeCount = primvars.size();
    }
    else {
        // Get the full attributes list defined in this primitive
        attributes = prim.GetAttributes();
        attributeCount = attributes.size();
    }

    bool isShape = (AiNodeEntryGetType(nodeEntry) == AI_NODE_SHAPE);

    // We currently support the following namespaces for arnold input attributes
    TfToken scopeToken(scope);
    for (size_t i = 0; i < attributeCount; ++i) {
        // The attribute can either come from the attributes list, or from the primvars list
        const UsdAttribute &attr = (readPrimvars) ? primvars[i].GetAttr() : attributes[i];
        if (!attr.HasAuthoredValue() && !attr.HasAuthoredConnections())
            continue;

        TfToken attrNamespace = attr.GetNamespace();
        std::string attrNamespaceStr = attrNamespace.GetString();
        std::string arnoldAttr = attr.GetBaseName().GetString();
        if (arnoldAttr.empty())
            continue;

        if (attrNamespace != scope) { // only deal with attributes of the desired scope

            bool namespaceIncludesScope = (!scope.empty()) && (scope.length() < attrNamespaceStr.length()) &&
                                          (attrNamespaceStr.find(scope) == 0);

            if (isShape && namespaceIncludesScope) {
                // Special case for ray-type visibility flags that can appedar as
                // visibility:camera, sidedness:shadow, etc... see #637
                std::string lastToken = attrNamespaceStr.substr(scope.length() + 1);
                if (lastToken == "visibility" || lastToken == "sidedness" || lastToken == "autobump_visibility") {
                    VtValue value;
                    if (attr.Get(&value, time.frame))
                        _SetRayFlag(node, lastToken, arnoldAttr, value);
                }                    
            }
            
            // Linked array attributes : This isn't supported natively in USD, so we need
            // to read it in a specific format. If attribute "attr" has element 1 linked to
            // a shader, we will write it as attr:i1
            if (arnoldAttr[0] == 'i' && (scope.empty() || namespaceIncludesScope)) {
                ReadArrayLink(prim, attr, time, context, node, scope);
            }
            continue;
        }

        if (arnoldAttr == "name") {
            // If attribute "name" is set in the usd prim, we need to set the node name
            // accordingly. We also store this node original name in a map, that we
            // might use later on, when processing connections.
            VtValue nameValue;
            if (attr.Get(&nameValue, time.frame)) {
                std::string nameStr = VtValueGetString(nameValue);
                std::string usdName = prim.GetPath().GetText();
                if ((!nameStr.empty()) && nameStr != usdName) {
                    AiNodeSetStr(node, str::name, AtString(nameStr.c_str()));
                    context.AddNodeName(usdName, node);
                }
            }
            continue;
        }
        
        const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(arnoldAttr.c_str()));
        if (paramEntry == nullptr) {
            // For custom procedurals, there will be an attribute node_entry that should be ignored.
            // In any other case, let's dump a warning
            if (arnoldAttr != "node_entry" || AiNodeEntryGetDerivedType(nodeEntry) != AI_NODE_SHAPE_PROCEDURAL) {
                AiMsgWarning(
                    "USD arnold attribute %s not recognized in %s for %s", arnoldAttr.c_str(), AiNodeEntryGetName(nodeEntry), AiNodeGetName(node));
            }
            continue;
        }
        uint8_t paramType = AiParamGetType(paramEntry);

        int arrayType = AI_TYPE_NONE;
        if (paramType == AI_TYPE_ARRAY) {
            const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
            // Getting the default array, and checking its type
            arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;
        }
        ReadAttribute(attr, node, arnoldAttr, time, context, paramType, arrayType);
    }
}


bool HasAuthoredAttribute(const UsdPrim &prim, const TfToken &attrName)
{
    if (!prim || !prim.HasAttribute(attrName))
        return false;

    UsdAttribute attr = prim.GetAttribute(attrName);
    if (attr && attr.HasAuthoredValue())
        return true;
    
    return false;
}


template <typename CastTo, typename CastFrom>
inline bool _VtValueGet(const VtValue& value, CastTo& data)
{    
    using CastFromType = typename std::remove_cv<typename std::remove_reference<CastFrom>::type>::type;
    using CastToType = typename std::remove_cv<typename std::remove_reference<CastTo>::type>::type;
    if (value.IsHolding<CastFromType>()) {
        ConvertValue(data, value.UncheckedGet<CastFromType>());
        return true;
    } else if (value.IsHolding<VtArray<CastFromType>>()) {
        const auto& arr = value.UncheckedGet<VtArray<CastFromType>>();
        if (!arr.empty()) {
            ConvertValue(data, arr[0]);
            return true;
        }
    }
    return false;
}

template <typename CastTo>
inline bool _VtValueGetRecursive(const VtValue& value, CastTo& data)
{
    return false;
}
template <typename CastTo, typename CastFrom, typename... CastRemaining>
inline bool _VtValueGetRecursive(const VtValue& value, CastTo& data)
{
    return _VtValueGet<CastTo, CastFrom>(value, data) || 
           _VtValueGetRecursive<CastTo, CastRemaining...>(value, data);
}

template <typename CastTo, typename CastFrom, typename... CastRemaining>
inline bool VtValueGet(const VtValue& value, CastTo& data)
{    
    return _VtValueGet<CastTo, CastTo>(value, data) || 
           _VtValueGetRecursive<CastTo, CastFrom, CastRemaining...>(value, data);
}

bool VtValueGetBool(const VtValue& value, bool defaultValue)
{   
    if (!value.IsEmpty())
        VtValueGet<bool, int, unsigned int, char, unsigned char, long, unsigned long>(value, defaultValue);
    return defaultValue;
}

float VtValueGetFloat(const VtValue& value, float defaultValue)
{
    if (!value.IsEmpty())
        VtValueGet<float, double, GfHalf>(value, defaultValue);

    return defaultValue;
}

unsigned char VtValueGetByte(const VtValue& value, unsigned char defaultValue)
{
    if (!value.IsEmpty())
        VtValueGet<unsigned char, int, unsigned int, uint8_t, char, long, unsigned long>(value, defaultValue);

    return defaultValue;
}

int VtValueGetInt(const VtValue& value, int defaultValue)
{
    if (!value.IsEmpty())
        VtValueGet<int, long, unsigned int, unsigned char, char, unsigned long>(value, defaultValue);

    return defaultValue;
}

unsigned int VtValueGetUInt(const VtValue& value, unsigned int defaultValue)
{
    if (!value.IsEmpty())
        VtValueGet<unsigned int, int, unsigned char, char, unsigned long, long>(value, defaultValue);

    return defaultValue;
}

GfVec2f VtValueGetVec2f(const VtValue& value, GfVec2f defaultValue)
{
    if (value.IsEmpty())
        return defaultValue;
    if (!VtValueGet<GfVec2f, GfVec2d, GfVec2h>(value, defaultValue)) {
        GfVec4f vec4(0.f);
        GfVec3f vec3(0.f);
        float flt = 0.f;
        if (VtValueGet<float, double, GfHalf>(value, flt))
            defaultValue = GfVec2f(flt, flt);
        else if (VtValueGet<GfVec3f, GfVec3d, GfVec3h>(value, vec3))
            defaultValue = GfVec2f(vec3[0], vec3[1]);
        else if (VtValueGet<GfVec4f, GfVec4d, GfVec4h>(value, vec4))
            defaultValue = GfVec2f(vec4[0], vec4[1]);        
    }
    return defaultValue;
}

GfVec3f VtValueGetVec3f(const VtValue& value, GfVec3f defaultValue)
{
    if (value.IsEmpty())
        return defaultValue;

    if (!VtValueGet<GfVec3f, GfVec3d, GfVec3h>(value, defaultValue)) {
        GfVec4f vec4(0.f);
        GfVec2f vec2(0.f);
        float flt = 0.f;
        if (VtValueGet<GfVec4f, GfVec4d, GfVec4h>(value, vec4))
            defaultValue = GfVec3f(vec4[0], vec4[1], vec4[2]);
        else if (VtValueGet<GfVec2f, GfVec2d, GfVec2h>(value, vec2))
            defaultValue = GfVec3f(vec2[0], vec2[1], 0.f);
        else if (VtValueGet<float, double, GfHalf>(value, flt))
            defaultValue = GfVec3f(flt, flt, flt);
    }
    return defaultValue;
}

GfVec4f VtValueGetVec4f(const VtValue& value, GfVec4f defaultValue)
{
    if (value.IsEmpty())
        return defaultValue;

    if (!VtValueGet<GfVec4f, GfVec4d, GfVec4h>(value, defaultValue)) {
        GfVec3f vec3(0.f);
        if (VtValueGet<GfVec3f, GfVec3d, GfVec3h>(value, vec3))
            defaultValue = GfVec4f(vec3[0], vec3[1], vec3[2], 1.f);
    }
    return defaultValue;
}

std::string VtValueGetString(const VtValue& value)
{
    std::string result;
    if (value.IsEmpty())
        return result;
    
    VtValueGet<std::string, TfToken, SdfAssetPath>(value, result);
    return result;
}

AtMatrix VtValueGetMatrix(const VtValue& value)
{
    if (value.IsEmpty())
        return AiM4Identity();
    AtMatrix result;
    _VtValueGetRecursive<AtMatrix, GfMatrix4f, GfMatrix4d>(value, result);
    return result;
}


template <typename CastTo, typename CastFrom>
inline AtArray *_VtValueGetArray(const std::vector<VtValue>& values, uint8_t arnoldType)
{    
    using CastFromType = typename std::remove_cv<typename std::remove_reference<CastFrom>::type>::type;
    using CastToType = typename std::remove_cv<typename std::remove_reference<CastTo>::type>::type;

    bool sameData = std::is_same<CastToType, CastFromType>::value;

    if (values[0].IsHolding<CastFromType>()) {
        const size_t numValues = values.size();
        AtArray *array = nullptr;
        CastToType *arrayData = nullptr;
        for (const auto& value : values) {
            const auto& v = value.UncheckedGet<CastFromType>();
            if (arrayData == nullptr) {
                array = AiArrayAllocate(1, numValues, arnoldType);
                arrayData = reinterpret_cast<CastToType*>(AiArrayMap(array));
            }
            ConvertValue(*arrayData, v);
            arrayData++;
        }
        if (array)
            AiArrayUnmap(array);
        return array;
    } else if (values[0].IsHolding<VtArray<CastFromType>>()) {
        const size_t numValues = values.size();
        AtArray *array = nullptr;
        CastToType *arrayData = nullptr;
        for (const auto& value : values) {
            const auto& v = value.UncheckedGet<VtArray<CastFromType>>();
            if (arrayData == nullptr) {
                array = AiArrayAllocate(v.size(), numValues, arnoldType);
                arrayData = reinterpret_cast<CastToType*>(AiArrayMap(array));
            }
            if (sameData) {
                memcpy(arrayData, v.data(), v.size() * sizeof(CastFromType));
                arrayData += v.size();
            } else {
                for (const auto vElem : v) {
                    ConvertValue(*arrayData, vElem);
                    arrayData++;
                }
            }
        }
        AiArrayUnmap(array);
        return array;
    }
    
    return nullptr;
}

template <typename CastTo>
inline AtArray *_VtValueGetArrayRecursive(const std::vector<VtValue>& values, uint8_t arnoldType)
{
    return nullptr;
}
template <typename CastTo, typename CastFrom, typename... CastRemaining>
inline AtArray *_VtValueGetArrayRecursive(const std::vector<VtValue>& values, uint8_t arnoldType)
{
    AtArray *arr = _VtValueGetArray<CastTo, CastFrom>(values, arnoldType);
    if (arr != nullptr)
        return arr;

    return _VtValueGetArrayRecursive<CastTo, CastRemaining...>(values, arnoldType);
}

template <typename CastTo, typename CastFrom, typename... CastRemaining>
inline AtArray *VtValueGetArray(const std::vector<VtValue>& values, uint8_t arnoldType)
{   
    AtArray *arr = _VtValueGetArray<CastTo, CastTo>(values, arnoldType);
    if (arr !=  nullptr)
        return arr;

    return _VtValueGetArrayRecursive<CastTo, CastFrom, CastRemaining...>(values, arnoldType);
}

AtArray *VtValueGetArray(const std::vector<VtValue>& values, uint8_t arnoldType, 
    ArnoldAPIAdapter &context)
{   
    if (values.empty())
        return nullptr;
    
    switch(arnoldType) {
        case AI_TYPE_INT:
        case AI_TYPE_ENUM:
            return VtValueGetArray<int, long, unsigned int, unsigned char, char, unsigned long>(values, arnoldType);
        case AI_TYPE_UINT:
            return VtValueGetArray<unsigned int, int, unsigned char, char, unsigned long, long>(values, arnoldType);
        case AI_TYPE_BOOLEAN:
            return VtValueGetArray<bool, int, unsigned int, char, unsigned char, long, unsigned long>(values, arnoldType);
        case AI_TYPE_FLOAT:
        case AI_TYPE_HALF:
            return VtValueGetArray<float, double, GfHalf>(values, arnoldType);
        case AI_TYPE_BYTE:
            return VtValueGetArray<unsigned char, int, unsigned int, uint8_t, char, long, unsigned long>(values, arnoldType);
        case AI_TYPE_VECTOR:
        case AI_TYPE_RGB:
            return VtValueGetArray<GfVec3f, GfVec3d, GfVec3h>(values, arnoldType);
        case AI_TYPE_RGBA:
            return VtValueGetArray<GfVec4f, GfVec4d, GfVec4h>(values, arnoldType);
        case AI_TYPE_VECTOR2:
            return VtValueGetArray<GfVec2f, GfVec2d, GfVec2h>(values, arnoldType);
        case AI_TYPE_MATRIX:
            return _VtValueGetArrayRecursive<AtMatrix, GfMatrix4f, GfMatrix4d>(values, arnoldType);
        // For node attributes, return a string array
        case AI_TYPE_NODE:
            arnoldType = AI_TYPE_STRING;
        case AI_TYPE_STRING:
            return _VtValueGetArrayRecursive<AtString, std::string, TfToken, SdfAssetPath>(values, arnoldType);
        default:
            break;
    }
    return nullptr;
}

std::string VtValueResolvePath(const SdfAssetPath& assetPath) {
    std::string path = assetPath.GetResolvedPath();
    if (path.empty())
        path = assetPath.GetAssetPath();
    return path;
}

template <typename T>
inline bool _HasValueType(const VtValue& value, bool type, bool arrayType)
{
    if (type && value.IsHolding<T>())
        return true;
    if (arrayType && value.IsHolding<VtArray<T>>())
        return true;
    return false;
}

template <typename T, typename U, typename... V>
inline bool _HasValueType(const VtValue& value, bool type, bool arrayType)
{
    return _HasValueType<T>(value, type, arrayType) || 
           _HasValueType<U, V...>(value, type, arrayType);
}

uint8_t GetArnoldTypeFromValue(const VtValue& value, bool type, bool arrayType)
{
    if (_HasValueType<bool>(value, type, arrayType))
        return AI_TYPE_BOOLEAN;
    
    if (_HasValueType<unsigned char>(value, type, arrayType))
        return AI_TYPE_BYTE;

    if (_HasValueType<unsigned int, unsigned long>(value, type, arrayType))
        return AI_TYPE_UINT;

    if (_HasValueType<int, long>(value, type, arrayType))
        return AI_TYPE_INT;

    if (_HasValueType<float, double, GfHalf>(value, type, arrayType))
        return AI_TYPE_FLOAT;

    if (_HasValueType<GfVec2f, GfVec2d, GfVec2h>(value, type, arrayType))
        return AI_TYPE_VECTOR2;

    if (_HasValueType<GfVec3f, GfVec3d, GfVec3h>(value, type, arrayType))
        return AI_TYPE_VECTOR; // can also be AI_TYPE_RGB

    if (_HasValueType<GfVec4f, GfVec4d, GfVec4h>(value, type, arrayType))
        return AI_TYPE_RGBA;

    if (_HasValueType<std::string, TfToken, SdfAssetPath>(value, type, arrayType))
        return AI_TYPE_STRING;

    if (_HasValueType<GfMatrix4f, GfMatrix4d>(value, type, arrayType))
        return AI_TYPE_MATRIX;

    return AI_TYPE_NONE;
}

bool DeclareArnoldAttribute(AtNode* node, const char *name, const char *scope, const char *type)
{
    const AtString nameStr{name};
    // If the attribute already exists (either as a node entry parameter
    // or as a user data in the node), then we should not call AiNodeDeclare
    // as it would fail.
    const AtNodeEntry* nentry = AiNodeGetNodeEntry(node);
    if (AiNodeEntryLookUpParameter(nentry, nameStr)) {
        AiNodeResetParameter(node, nameStr);
        return true;
    }

    if (AiNodeLookUpUserParameter(node, nameStr)) {
        // For user parameters we want to ensure we're not resetting an index array
        size_t nameLength = nameStr.length();
        if (nameLength > 4 && strcmp(name + nameLength - 4, "idxs") == 0) {
            AtString attrPrefixStr(std::string(name).substr(0, nameLength - 4).c_str());
            const AtUserParamEntry* paramEntry = AiNodeLookUpUserParameter(node, attrPrefixStr);
            if (paramEntry && AiUserParamGetCategory(paramEntry) == AI_USERDEF_INDEXED) {
                return true;
            }
        }

        AiNodeResetParameter(node, nameStr);
    }
    return AiNodeDeclare(node, nameStr, AtString(TfStringPrintf("%s %s", scope, type).c_str()));
}
// As opposed to ReadAttribute that takes an input arnold attribute, and determines how to read the VtValue,
// this function takes a VtValue as an input and determines the arnold type based on it

uint32_t DeclareAndAssignParameter(
    AtNode* node, const TfToken& name, const TfToken& scope, const VtValue& value, ArnoldAPIAdapter &context, bool isColor)
{   
    if (value.IsEmpty())
        return 0;

    bool isArray = value.IsArrayValued();
    size_t arraySize = value.GetArraySize();
    
    // - If the value is not an array, we want a constant user data
    // - If the value has a single element, and the scope is "constant", we want a constant user data.
    // - If the value has more than one element, and the scope is "constant", we want a constant array
    // - If the attribute name is "displayColor" and has a single element, we want a constant user data
    bool isConstant = !isArray || 
        (scope == str::t_constant || name == str::t_displayColor) && arraySize <= 1;
    
    uint8_t type = GetArnoldTypeFromValue(value, !isArray , isArray);
    
    if (type == AI_TYPE_NONE)
        return 0;

    if (isColor && type == AI_TYPE_VECTOR)
        type = AI_TYPE_RGB;

    if (!DeclareArnoldAttribute(node, name.GetText(), 
            (isConstant) ? str::constant.c_str() : 
            (scope == str::t_constant && arraySize > 1) ? str::constantArray.c_str() : 
            scope.GetText(),
            AiParamGetTypeName(type)))
        return 0;

    uint8_t paramType = isConstant ? type : AI_TYPE_ARRAY;
    uint8_t arrayType = isConstant ? AI_TYPE_NONE : type;

    InputAttribute attr;
    attr.value = value;
    TimeSettings time;

    ReadAttribute(attr, node, name.GetString(), 
        time, context, paramType, arrayType);
    return isConstant ? 1 : arraySize;
}

