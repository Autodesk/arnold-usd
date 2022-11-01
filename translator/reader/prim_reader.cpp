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
#include <pxr/usd/usdShade/nodeGraph.h>
#include <pxr/usd/usd/primCompositionQuery.h>

#include <pxr/usd/pcp/layerStack.h>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

#include <constant_strings.h>

#include "prim_reader.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// Unsupported node types should dump a warning when being converted
void UsdArnoldReadUnsupported::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AiMsgWarning(
        "UsdArnoldReader : %s primitives not supported, cannot read %s", _typeName.c_str(), prim.GetName().GetText());
}

// Node & node array attributes are saved as strings, pointing to the arnold node name.
// But if this usd file is referenced from another one, it will automatically be added a prefix by USD
// composition arcs, and thus we won't be able to find the proper arnold node name based on its name.
// ValidatePrimPath handles this, by eventually adjusting the prim path 
void UsdArnoldPrimReader::ValidatePrimPath(std::string &path, const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    SdfPath sdfPath(path.c_str());
    UsdPrim targetPrim = context.GetReader()->GetStage()->GetPrimAtPath(sdfPath);
    // the prim path already exists, nothing to do
    if (targetPrim)
        return;

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
                if (context.GetReader()->GetStage()->GetPrimAtPath(sdfPath))
                {
                    path = composedName;
                    return;
                }       
            }
        }
    }
}
void UsdArnoldPrimReader::ReadAttribute(
    const UsdPrim &prim, InputAttribute &attr, AtNode *node, const std::string &arnoldAttr, const TimeSettings &time,
    UsdArnoldReaderContext &context, int paramType, int arrayType)
{
    const UsdAttribute &usdAttr = attr.GetAttr();
    SdfValueTypeName typeName = usdAttr.GetTypeName();
    if (paramType == AI_TYPE_ARRAY) {
        if (arrayType == AI_TYPE_NONE) {
            // No array type provided, let's find it ourselves
            if (typeName == SdfValueTypeNames->UCharArray)
                arrayType = AI_TYPE_BYTE;
            else if (typeName == SdfValueTypeNames->IntArray)
                arrayType = AI_TYPE_INT;
            else if (typeName == SdfValueTypeNames->UIntArray)
                arrayType = AI_TYPE_UINT;
            else if (typeName == SdfValueTypeNames->BoolArray)
                arrayType = AI_TYPE_BOOLEAN;
            else if (typeName == SdfValueTypeNames->FloatArray || typeName == SdfValueTypeNames->DoubleArray)
                arrayType = AI_TYPE_FLOAT;
            else if (typeName == SdfValueTypeNames->Float2Array)
                arrayType = AI_TYPE_VECTOR2;
            else if (typeName == SdfValueTypeNames->Vector3fArray || typeName == SdfValueTypeNames->Float3Array)
                arrayType = AI_TYPE_VECTOR;
            else if (typeName == SdfValueTypeNames->Color3fArray)
                arrayType = AI_TYPE_RGB;
            else if (typeName == SdfValueTypeNames->Color4fArray)
                arrayType = AI_TYPE_RGBA;
            else if (typeName == SdfValueTypeNames->StringArray || typeName == SdfValueTypeNames->TokenArray)
                arrayType = AI_TYPE_STRING;
            else if (typeName == SdfValueTypeNames->Matrix4dArray)
                arrayType = AI_TYPE_MATRIX;
        }

        // The default array has a type, let's do the conversion based on it
        switch (arrayType) {
            case AI_TYPE_BYTE:
                ReadArray<unsigned char, unsigned char>(attr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_INT:
                ReadArray<int, int>(attr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_UINT:
                ReadArray<unsigned int, unsigned int>(attr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_BOOLEAN:
                ReadArray<bool, bool>(attr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_FLOAT:
                if (typeName == SdfValueTypeNames->DoubleArray)
                    ReadArray<double, float>(attr, node, arnoldAttr.c_str(), time);
                else
                    ReadArray<float, float>(attr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_VECTOR:
            case AI_TYPE_RGB:
                // Vector and RGB are represented as GfVec3f, so we need to pass the type
                // (AI_TYPE_VECTOR / AI_TYPE_RGB) so that the arnold array is set properly #325
                ReadArray<GfVec3f, GfVec3f>(attr, node, arnoldAttr.c_str(), time, arrayType);
                break;
            case AI_TYPE_RGBA:
                ReadArray<GfVec4f, GfVec4f>(attr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_VECTOR2:
                ReadArray<GfVec2f, GfVec2f>(attr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_ENUM:
            case AI_TYPE_STRING:
                ReadStringArray(usdAttr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_MATRIX:
                ReadArray<GfMatrix4d, GfMatrix4d>(attr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_NODE: {
                std::string serializedArray;
                VtArray<std::string> array;
                usdAttr.Get(&array, time.frame);
                for (size_t v = 0; v < array.size(); ++v) {
                    std::string nodeName = array[v];
                    if (nodeName.empty()) {
                        continue;
                    }
                    ValidatePrimPath(nodeName, prim, context);
                    if (!serializedArray.empty())
                        serializedArray += std::string(" ");
                    serializedArray += nodeName;
                }

                context.AddConnection(node, arnoldAttr, serializedArray, UsdArnoldReader::CONNECTION_ARRAY);
                break;
            }
            default:
                break;
        }
    } else {
        VtValue vtValue;
        if (attr.Get(&vtValue, time.frame)) {
            
            // Simple parameters (not-an-array)
            switch (paramType) {
                case AI_TYPE_BYTE:
                    AiNodeSetByte(node, AtString(arnoldAttr.c_str()), VtValueGetByte(vtValue));
                    break;
                case AI_TYPE_INT:
                    AiNodeSetInt(node, AtString(arnoldAttr.c_str()), VtValueGetInt(vtValue));
                    break;
                case AI_TYPE_UINT:
                case AI_TYPE_USHORT:
                    AiNodeSetUInt(node, AtString(arnoldAttr.c_str()), VtValueGetUInt(vtValue));
                    break;
                case AI_TYPE_BOOLEAN:
                    AiNodeSetBool(node, AtString(arnoldAttr.c_str()), VtValueGetBool(vtValue));
                    break;
                case AI_TYPE_FLOAT:
                case AI_TYPE_HALF:
                    AiNodeSetFlt(node, AtString(arnoldAttr.c_str()), VtValueGetFloat(vtValue));
                    break;

                case AI_TYPE_VECTOR: {
                    GfVec3f vec = VtValueGetVec3f(vtValue);
                    AiNodeSetVec(node, AtString(arnoldAttr.c_str()), vec[0], vec[1], vec[2]);
                    break;
                }
                case AI_TYPE_RGB: {
                    GfVec3f vec = VtValueGetVec3f(vtValue);
                    AiNodeSetRGB(node, AtString(arnoldAttr.c_str()), vec[0], vec[1], vec[2]);
                    break;
                }

                case AI_TYPE_RGBA: {
                    GfVec4f vec = VtValueGetVec4f(vtValue);
                    AiNodeSetRGBA(node, AtString(arnoldAttr.c_str()), vec[0], vec[1], vec[2], vec[3]);
                    break;
                }
                case AI_TYPE_VECTOR2: {
                    GfVec2f vec = VtValueGetVec2f(vtValue);
                    AiNodeSetVec2(node, AtString(arnoldAttr.c_str()), vec[0], vec[1]);
                    break;
                }
                case AI_TYPE_ENUM:
                    if (vtValue.IsHolding<int>()) {
                        AiNodeSetInt(node, AtString(arnoldAttr.c_str()), vtValue.UncheckedGet<int>());
                        break;
                    } else if (vtValue.IsHolding<long>()) {
                        AiNodeSetInt(node, AtString(arnoldAttr.c_str()), vtValue.UncheckedGet<long>());
                        break;
                    }
                // Enums can be strings, so we don't break here.
                case AI_TYPE_STRING: {
                    std::string str = VtValueGetString(vtValue, &usdAttr);
                    AiNodeSetStr(node, AtString(arnoldAttr.c_str()), AtString(str.c_str()));
                    break;
                }
                case AI_TYPE_MATRIX: {
                    AtMatrix aiMat;
                    if (VtValueGetMatrix(vtValue, aiMat))
                        AiNodeSetMatrix(node, AtString(arnoldAttr.c_str()), aiMat);
                    break;
                }
                // node attributes are expected as strings
                case AI_TYPE_NODE: {
                    std::string nodeName = VtValueGetString(vtValue, &usdAttr);
                    if (!nodeName.empty()) {
                        ValidatePrimPath(nodeName, prim, context);
                        context.AddConnection(node, arnoldAttr, nodeName, UsdArnoldReader::CONNECTION_PTR);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        // check if there are connections to this attribute
        bool isImager = AiNodeEntryGetType(AiNodeGetNodeEntry(node)) == AI_NODE_DRIVER;
        if ((paramType != AI_TYPE_NODE || isImager) && usdAttr.HasAuthoredConnections())
            _ReadAttributeConnection(prim, usdAttr, node, arnoldAttr, time, context, paramType);
    }
}

void UsdArnoldPrimReader::_ReadAttributeConnection(
    const UsdPrim &prim, const UsdAttribute &usdAttr, AtNode *node, const std::string &arnoldAttr,  const TimeSettings &time, 
    UsdArnoldReaderContext &context, int paramType)
{
    SdfPathVector targets;
    usdAttr.GetConnections(&targets);
    if (targets.empty())
        return;
    
    SdfPath &target = targets[0]; // just consider the first target
    SdfPath primPath = target.GetPrimPath();

    std::string outputElement;
    if (!target.IsPrimPath()) {
        outputElement = target.GetElementString();
        if (!outputElement.empty() && outputElement[0] == '.')
            outputElement = outputElement.substr(1);

        // We need to check if this attribute connection is pointing at a nodeGraph primitive.
        // If so, we need to read it as if our attribute was actually the nodeGraph one.
        // It could either be set to a constant value, or it could itself be connected to
        // another shader. 
        UsdPrim targetPrim = context.GetReader()->GetStage()->GetPrimAtPath(primPath);
        if (targetPrim && targetPrim.IsA<UsdShadeNodeGraph>()) {
            UsdAttribute nodeGraphAttr = targetPrim.GetAttribute(TfToken(outputElement));
            if (nodeGraphAttr) {
                InputAttribute inputAttr(nodeGraphAttr);
                ReadAttribute(prim, inputAttr, node, arnoldAttr, time, context, paramType, AI_TYPE_NONE);
            }
            return;
        }
    }

    // if it's an imager then use a CONNECTION_PTR
    context.AddConnection(node, arnoldAttr, target.GetPrimPath().GetText(),
                          AiNodeEntryGetType(AiNodeGetNodeEntry(node)) == AI_NODE_DRIVER ?
                            UsdArnoldReader::CONNECTION_PTR : UsdArnoldReader::CONNECTION_LINK,
                          outputElement);
}

void UsdArnoldPrimReader::_ReadArrayLink(
    const UsdPrim &prim, const UsdAttribute &attr, const TimeSettings &time, 
    UsdArnoldReaderContext &context, AtNode *node, const std::string &scope)
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

    _ReadAttributeConnection(prim, attr, node, attrElemName, time, context, AI_TYPE_ARRAY);
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
 *based on its type. The input attribute acceptEmptyScope is for backward compatibility,
 *in order to keep supporting usd files authored with previous versions of arnold-usd.
 * (before #583). It's meant to be removed
 **/
void UsdArnoldPrimReader::ReadArnoldParameters(
    const UsdPrim &prim, UsdArnoldReaderContext &context, AtNode *node, const TimeSettings &time,
    const std::string &scope, bool acceptEmptyScope)
{    
    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
    if (nodeEntry == nullptr) {
        return; // shouldn't happen
    }


    bool isOsl = AiNodeIs(node, str::osl);
    if (isOsl) {
        UsdAttribute oslCode = prim.GetAttribute(str::t_inputs_code);
        VtValue value;
        if (oslCode && oslCode.Get(&value, time.frame)) {
            std::string code = VtValueGetString(value, &oslCode);
            if (!code.empty()) {
                AiNodeSetStr(node, str::code, AtString(code.c_str()));
                // Need to update the node entry that was
                // modified after "code" is set
                nodeEntry = AiNodeGetNodeEntry(node);
            }
        }
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
                _ReadArrayLink(prim, attr, time, context, node, scope);
            }
            // this flag acceptEmptyScope is temporary and meant to be removed
            if (!acceptEmptyScope || !attrNamespace.GetString().empty())
                continue;
        }
        if (isOsl && arnoldAttr == "code")
            continue;

        if (arnoldAttr == "name") {
            // If attribute "name" is set in the usd prim, we need to set the node name
            // accordingly. We also store this node original name in a map, that we
            // might use later on, when processing connections.
            VtValue nameValue;
            if (attr.Get(&nameValue, time.frame)) {
                std::string nameStr = VtValueGetString(nameValue, &attr);
                std::string usdName = prim.GetPath().GetText();
                if ((!nameStr.empty()) && nameStr != usdName) {
                    AiNodeSetStr(node, str::name, AtString(nameStr.c_str()));
                    context.AddNodeName(usdName, node);
                }
            }
            continue;
        }
        if (acceptEmptyScope && arnoldAttr == "xformOpOrder")
            continue;

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

        InputAttribute inputAttr(attr);

        ReadAttribute(prim, inputAttr, node, arnoldAttr, time, context, paramType, arrayType);
    }
}

/**
 *  Read all primvars from this shape, and set them as arnold user data
 *
 **/

void UsdArnoldPrimReader::ReadPrimvars(
    const UsdPrim &prim, AtNode *node, const TimeSettings &time, UsdArnoldReaderContext &context, 
    PrimvarsRemapper *primvarsRemapper)
{
    assert(prim);
    UsdGeomPrimvarsAPI primvarsAPI = UsdGeomPrimvarsAPI(prim);
    if (!primvarsAPI)
        return;

    float frame = time.frame;
    // copy the time settings, as we might want to disable motion blur
    TimeSettings attrTime(time);
    
    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
    bool isPolymesh = AiNodeIs(node, str::polymesh);
    bool isPoints = (isPolymesh) ? false : AiNodeIs(node, str::points);
    
    // First, we'll want to consider all the primvars defined in this primitive
    std::vector<UsdGeomPrimvar> primvars = primvarsAPI.GetPrimvars();
    size_t primvarsSize = primvars.size();
    // Then, we'll also want to use the primvars that were accumulated over this prim hierarchy,
    // and that only included constant primvars. Note that all the constant primvars defined in 
    // this primitive will appear twice in the full primvars list, so we'll skip them during the loop
    const std::vector<UsdGeomPrimvar> &inheritedPrimvars = context.GetPrimvars();
    primvars.insert(primvars.end(), inheritedPrimvars.begin(), inheritedPrimvars.end());

    for (size_t i = 0; i < primvars.size(); ++i) {
        const UsdGeomPrimvar &primvar = primvars[i];

        // ignore primvars starting with arnold: as they will be loaded separately.
        // same for other namespaces
        if (primvar.NameContainsNamespaces())
            continue;

        TfToken interpolation = primvar.GetInterpolation();
        // Find the declaration based on the interpolation type
        std::string declaration =
            (interpolation == UsdGeomTokens->uniform)
                ? "uniform"
                : (interpolation == UsdGeomTokens->varying)
                      ? "varying"
                      : (interpolation == UsdGeomTokens->vertex)
                            ? "varying"
                            : (interpolation == UsdGeomTokens->faceVarying) ? "indexed" : "constant";


        // We want to ignore the constant primvars returned by primvarsAPI.GetPrimvars(),
        // because they'll also appear in the second part of the list, coming from inheritedPrimvars
        if (i < primvarsSize && interpolation == UsdGeomTokens->constant)
            continue;
        
        TfToken name = primvar.GetPrimvarName();
        if ((name == "displayColor" || name == "displayOpacity") && !primvar.GetAttr().HasAuthoredValue())
            continue;

        // if this parameter already exists, we want to skip it
        if (AiNodeEntryLookUpParameter(nodeEntry, AtString(name.GetText())) != nullptr)
            continue;

        // A remapper can eventually remap the interpolation (e.g. point instancer)
        if (primvarsRemapper)
            primvarsRemapper->RemapPrimvar(name, declaration);

        SdfValueTypeName typeName = primvar.GetTypeName();        
        std::string arnoldIndexName = name.GetText() + std::string("idxs");

        int primvarType = AI_TYPE_NONE;

        //  In Arnold, points with user-data per-point are considered as being "uniform" (one value per face).
        //  We must ensure that we're not setting varying user data on the points or this will fail (see #228)
        if (isPoints && declaration == "varying")
            declaration = "uniform";

        if (typeName == SdfValueTypeNames->Float2 || typeName == SdfValueTypeNames->Float2Array ||
            typeName == SdfValueTypeNames->TexCoord2f || typeName == SdfValueTypeNames->TexCoord2fArray) {
            primvarType = AI_TYPE_VECTOR2;

            // A special case for UVs
            if (isPolymesh && (name == "uv" || name == "st")) {
                name = str::t_uvlist;
                // Arnold doesn't support motion blurred UVs, this is causing an error (#780),
                // let's disable it for this attribute
                attrTime.motionBlur = false;
                arnoldIndexName = "uvidxs";
                // In USD the uv coordinates can be per-vertex. In that case we won't have any "uvidxs"
                // array to give to the arnold polymesh, and arnold will error out. We need to set an array
                // that is identical to "vidxs" and returns the vertex index for each face-vertex
                if (interpolation == UsdGeomTokens->varying || (interpolation == UsdGeomTokens->vertex)) {
                    AiNodeSetArray(node, str::uvidxs, AiArrayCopy(AiNodeGetArray(node, str::vidxs)));
                }
            }
        } else if (
            typeName == SdfValueTypeNames->Vector3f || typeName == SdfValueTypeNames->Vector3fArray ||
            typeName == SdfValueTypeNames->Point3f || typeName == SdfValueTypeNames->Point3fArray ||
            typeName == SdfValueTypeNames->Normal3f || typeName == SdfValueTypeNames->Normal3fArray ||
            typeName == SdfValueTypeNames->Float3 || typeName == SdfValueTypeNames->Float3Array ||
            typeName == SdfValueTypeNames->TexCoord3f || typeName == SdfValueTypeNames->TexCoord3fArray) {
            primvarType = AI_TYPE_VECTOR;

            // Another special case for normals
            if (isPolymesh && name == "normals") {
                name = str::t_nlist;
                arnoldIndexName = "nidxs";
                // In USD the normals can be per-vertex. In that case we won't have any "nidxs"
                // array to give to the arnold polymesh, and arnold will error out. We need to set an array
                // that is identical to "vidxs" and returns the vertex index for each face-vertex
                if (interpolation == UsdGeomTokens->varying || (interpolation == UsdGeomTokens->vertex)) {
                    AiNodeSetArray(node, str::nidxs, AiArrayCopy(AiNodeGetArray(node, str::vidxs)));
                }
            }
        } else if (typeName == SdfValueTypeNames->Color3f || typeName == SdfValueTypeNames->Color3fArray)
            primvarType = AI_TYPE_RGB;
        else if (
            typeName == SdfValueTypeNames->Color4f || typeName == SdfValueTypeNames->Color4fArray ||
            typeName == SdfValueTypeNames->Float4 || typeName == SdfValueTypeNames->Float4Array)
            primvarType = AI_TYPE_RGBA;
        else if (typeName == SdfValueTypeNames->Float || typeName == SdfValueTypeNames->FloatArray || 
            typeName == SdfValueTypeNames->Double || typeName == SdfValueTypeNames->DoubleArray)
            primvarType = AI_TYPE_FLOAT;
        else if (typeName == SdfValueTypeNames->Int || typeName == SdfValueTypeNames->IntArray)
            primvarType = AI_TYPE_INT;
        else if (typeName == SdfValueTypeNames->UInt || typeName == SdfValueTypeNames->UIntArray)
            primvarType = AI_TYPE_UINT;
        else if (typeName == SdfValueTypeNames->UChar || typeName == SdfValueTypeNames->UCharArray)
            primvarType = AI_TYPE_BYTE;
        else if (typeName == SdfValueTypeNames->Bool || typeName == SdfValueTypeNames->BoolArray)
            primvarType = AI_TYPE_BOOLEAN;
        else if (typeName == SdfValueTypeNames->String || typeName == SdfValueTypeNames->StringArray) {
            // both string and node user data are saved to USD as string attributes, since there's no
            // equivalent in USD. To distinguish between these 2 use cases, we will also write a
            // connection between the string primvar and the node. This is what we use here to
            // determine the user data type.
            primvarType = (primvar.GetAttr().HasAuthoredConnections()) ? AI_TYPE_NODE : AI_TYPE_STRING;
        }

        if (primvarType == AI_TYPE_NONE)
            continue;

        int arrayType = AI_TYPE_NONE;
        
        if (typeName.IsArray() && interpolation == UsdGeomTokens->constant &&
            primvarType != AI_TYPE_ARRAY && primvar.GetElementSize() > 1) 
        {
            arrayType = primvarType;
            primvarType = AI_TYPE_ARRAY;
            declaration += " ARRAY ";
        }

        declaration += " ";
        declaration += AiParamGetTypeName(primvarType);

        
        AtString nameStr(name.GetText());
        if (AiNodeLookUpUserParameter(node, nameStr) == nullptr && 
            AiNodeEntryLookUpParameter(nodeEntry, nameStr) == nullptr) {
            AiNodeDeclare(node, nameStr, declaration.c_str());    
        }
            
        bool hasIdxs = false;

        // If the primvar is indexed, we need to set this as a
        if (interpolation == UsdGeomTokens->faceVarying) {
            VtIntArray vtIndices;
            std::vector<unsigned int> indexes;

            if (primvar.IsIndexed() && primvar.GetIndices(&vtIndices, frame) && !vtIndices.empty()) {
                // We need to use indexes and we can't use vtIndices because we
                // need unsigned int. Converting int to unsigned int.
                indexes.resize(vtIndices.size());
                std::copy(vtIndices.begin(), vtIndices.end(), indexes.begin());
            } else {
                // Arnold doesn't have facevarying iterpolation. It has indexed
                // instead. So it means it's necessary to generate indexes for
                // this type.
                // TODO: Try to generate indexes only once and use it for
                // several primvars.

                // Unfortunately elementSize is not giving us the value we need here,
                // so we need to get the VtValue just to find its size.
                // We also need to get the value from the inputAttribute and not from 
                // the primvar, as the later seems to return an empty list in some cases #621
                VtValue tmp;
                InputAttribute tmpAttr(primvar);
                if (tmpAttr.Get(&tmp, time.frame)) {
                    indexes.resize(tmp.GetArraySize());
                    // Fill it with 0, 1, ..., 99.
                    std::iota(std::begin(indexes), std::end(indexes), 0);
                }
            }
            if (!indexes.empty())
            {
                // If the mesh has left-handed orientation, we need to invert the
                // indices of primvars for each face
                if (primvarsRemapper)
                    primvarsRemapper->RemapIndexes(primvar, interpolation, indexes);
                
                AiNodeSetArray(
                    node, AtString(arnoldIndexName.c_str()), AiArrayConvert(indexes.size(), 1, AI_TYPE_UINT, indexes.data()));

                hasIdxs = true;
            }
        }

        // Deduce primvar type and array type.
        
        if (interpolation != UsdGeomTokens->constant && primvarType != AI_TYPE_ARRAY) {
            arrayType = primvarType;
            primvarType = AI_TYPE_ARRAY;
        
        }

        InputAttribute inputAttr(primvar);
        inputAttr.computeFlattened = (interpolation != UsdGeomTokens->constant && !hasIdxs);

        if (primvarsRemapper) {
            inputAttr.primvarsRemapper = primvarsRemapper;
            inputAttr.primvarInterpolation = interpolation;
        }
        ReadAttribute(prim, inputAttr, node, name.GetText(), attrTime, context, primvarType, arrayType);
    }
}
