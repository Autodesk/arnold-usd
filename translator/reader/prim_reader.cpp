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

#include <pxr/usd/usdGeom/primvarsAPI.h>
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

void UsdArnoldPrimReader::readAttribute(InputAttribute &attr, 
    AtNode *node, const std::string &arnoldAttr, const TimeSettings &time, 
    UsdArnoldReaderContext &context, int paramType, int arrayType)
{
    const UsdAttribute &usdAttr = attr.GetAttr();
    if (paramType == AI_TYPE_ARRAY) {

        if (arrayType == AI_TYPE_NONE) {
            // No array type provided, let's find it ourselves
            SdfValueTypeName typeName = usdAttr.GetTypeName();
            if (typeName == SdfValueTypeNames->UCharArray)
                arrayType = AI_TYPE_BYTE;
            else if (typeName == SdfValueTypeNames->IntArray)
                arrayType = AI_TYPE_INT;
            else if (typeName == SdfValueTypeNames->UIntArray)
                arrayType = AI_TYPE_UINT;
            else if (typeName == SdfValueTypeNames->BoolArray)
                arrayType = AI_TYPE_BOOLEAN;
            else if (typeName == SdfValueTypeNames->FloatArray)
                arrayType = AI_TYPE_FLOAT;
            else if (typeName == SdfValueTypeNames->Float2Array)
                arrayType = AI_TYPE_VECTOR2;
            else if (typeName == SdfValueTypeNames->Vector3fArray ||
                     typeName == SdfValueTypeNames->Float3Array ) 
                arrayType = AI_TYPE_VECTOR;
            else if (typeName == SdfValueTypeNames->Color3fArray)
                arrayType = AI_TYPE_RGB;
            else if (typeName == SdfValueTypeNames->Color4fArray)
                arrayType = AI_TYPE_RGBA;
            else if (typeName == SdfValueTypeNames->StringArray || 
                     typeName == SdfValueTypeNames->TokenArray)
                arrayType = AI_TYPE_STRING;
            else if (typeName == SdfValueTypeNames->Matrix4dArray)
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
                exportStringArray(usdAttr, node, arnoldAttr.c_str(), time);
                break;
            case AI_TYPE_MATRIX:
                exportArray<GfMatrix4d, GfMatrix4d>(attr, node, arnoldAttr.c_str(), time);
                break;
            {
            case AI_TYPE_NODE:
                std::string serializedArray;                    
                VtArray<std::string> array;
                usdAttr.Get(&array, time.frame);
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
        VtValue vtValue;
        if (attr.Get(&vtValue, time.frame)) {
            bool isArray = vtValue.IsArrayValued();

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
                    AiNodeSetUInt(node, arnoldAttr.c_str(), (isArray) ? 
                        vtValue.Get<VtArray<unsigned int>>()[0] : vtValue.Get<unsigned int>());
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
                    GfVec3f vec = (isArray) ? vtValue.Get<VtArray<GfVec3f>>()[0] : vtValue.Get<GfVec3f>();
                    AiNodeSetVec(node, arnoldAttr.c_str(), vec[0], vec[1], vec[2]);
                    break;
                }
                {
                case AI_TYPE_RGB:
                    GfVec3f vec = (isArray) ? vtValue.Get<VtArray<GfVec3f>>()[0] : vtValue.Get<GfVec3f>();
                    AiNodeSetRGB(node, arnoldAttr.c_str(), vec[0], vec[1], vec[2]);
                    break;
                }
                {
                case AI_TYPE_RGBA:
                    GfVec4f vec = (isArray) ? vtValue.Get<VtArray<GfVec4f>>()[0] : vtValue.Get<GfVec4f>();
                    AiNodeSetRGBA(node, arnoldAttr.c_str(), vec[0], vec[1], vec[2], vec[3]);
                    break;
                }
                {
                case AI_TYPE_VECTOR2:
                    GfVec2f vec = (isArray) ? vtValue.Get<VtArray<GfVec2f>>()[0] : vtValue.Get<GfVec2f>();
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
                    } else if (vtValue.IsHolding<VtArray<std::string>>()) {
                        std::string str = vtValue.UncheckedGet<VtArray<std::string>>()[0];
                        AiNodeSetStr(node, arnoldAttr.c_str(), str.c_str());
                    } else if (vtValue.IsHolding<VtArray<TfToken>>()) {
                        auto token = vtValue.UncheckedGet<VtArray<TfToken>>()[0];
                        AiNodeSetStr(node, arnoldAttr.c_str(), token.GetText());
                    }
                    break;
                }
                {
                case AI_TYPE_MATRIX:
                    GfMatrix4d usdMat = (isArray) ? 
                            vtValue.Get<VtArray<GfMatrix4d>>()[0] : vtValue.Get<GfMatrix4d>();
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
                    std::string nodeName = (isArray) ? 
                            vtValue.Get<VtArray<std::string>>()[0] : vtValue.Get<std::string>();
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
        }
        // check if there are connections to this attribute
        if (usdAttr.HasAuthoredConnections()) {
            SdfPathVector targets;
            usdAttr.GetConnections(&targets);
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

        int arrayType = AI_TYPE_NONE;
        if (paramType == AI_TYPE_ARRAY) {
            const AtParamValue *defaultValue = AiParamGetDefault(paramEntry);
            // Getting the default array, and checking its type
            arrayType = (defaultValue) ? AiArrayGetType(defaultValue->ARRAY()) : AI_TYPE_NONE;
        }

        InputAttribute inputAttr(attr);

        readAttribute(inputAttr, node, arnoldAttr, time, context, paramType, arrayType);
        
    }
}

/**
 *  Export all primvars from this shape, and set them as arnold user data
 *
 **/
void UsdArnoldPrimReader::exportPrimvars(const UsdPrim &prim, AtNode *node, const TimeSettings &time, 
            UsdArnoldReaderContext &context, MeshOrientation *orientation)
{
    assert(prim);
    UsdGeomPrimvarsAPI primvarsAPI = UsdGeomPrimvarsAPI(prim);
    if (!primvarsAPI)
        return;

    float frame = time.frame;

    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
    bool isPolymesh = (orientation != nullptr); // only polymeshes provide a Mesh orientation
    static AtString pointsStr("points");
    bool isPoints = (isPolymesh) ? false : AiNodeIs(node, pointsStr);
    const static AtString vidxsStr("vidxs");
    
    for (const UsdGeomPrimvar &primvar : primvarsAPI.GetPrimvars()) {
        TfToken name;
        SdfValueTypeName typeName;
        TfToken interpolation;
        int elementSize;

        primvar.GetDeclarationInfo(&name, &typeName, &interpolation, &elementSize);

        if ((name == "displayColor" || name == "displayOpacity") && !primvar.GetAttr().HasAuthoredValue())
            continue;
        
        // if we find a namespacing in the primvar name we skip it.
        // It's either an arnold attribute or it could be meant for another renderer
        if (name.GetString().find(':') != std::string::npos)
            continue;

        TfToken arnoldName = name;
        std::string arnoldIndexName = name.GetText() + std::string("idxs");

        int primvarType = AI_TYPE_NONE;
        // Find the declaration based on the interpolation type
        std::string declaration =
            (interpolation == UsdGeomTokens->uniform)
                ? "uniform "
                : (interpolation == UsdGeomTokens->varying)
                      ? "varying "
                      : (interpolation == UsdGeomTokens->vertex)
                            ? "varying "
                            : (interpolation == UsdGeomTokens->faceVarying) ? "indexed " : "constant ";

        //  In Arnold, points with user-data per-point are considered as being "uniform" (one value per face).
        //  We must ensure that we're not setting varying user data on the points or this will fail (see #228)
        if (isPoints && declaration == "varying ")
            declaration = "uniform ";

        if (typeName == SdfValueTypeNames->Float2 || typeName == SdfValueTypeNames->Float2Array) {
            primvarType = AI_TYPE_VECTOR2;

            // A special case for UVs
            if (isPolymesh && (name == "uv" || name == "st")) {
                arnoldName = TfToken("uvlist");
                arnoldIndexName = "uvidxs";
                // In USD the uv coordinates can be per-vertex. In that case we won't have any "uvidxs"
                // array to give to the arnold polymesh, and arnold will error out. We need to set an array
                // that is identical to "vidxs" and returns the vertex index for each face-vertex
                if (interpolation == UsdGeomTokens->varying ||  (interpolation == UsdGeomTokens->vertex)) {
                    AiNodeSetArray(node, "uvidxs", AiArrayCopy(AiNodeGetArray(node, vidxsStr)));
                }
            }
        } else if (typeName == SdfValueTypeNames->Vector3f || typeName == SdfValueTypeNames->Vector3fArray ||
                    typeName == SdfValueTypeNames->Float3 || typeName == SdfValueTypeNames->Float3Array) {
            primvarType = AI_TYPE_VECTOR;

            // Another special case for normals
            if (isPolymesh && name == "normals") {
                arnoldName = TfToken("nlist");
                arnoldIndexName = "nidxs";
                // In USD the normals can be per-vertex. In that case we won't have any "nidxs"
                // array to give to the arnold polymesh, and arnold will error out. We need to set an array
                // that is identical to "vidxs" and returns the vertex index for each face-vertex
                if (interpolation == UsdGeomTokens->varying ||  (interpolation == UsdGeomTokens->vertex)) {
                    AiNodeSetArray(node, "nidxs", AiArrayCopy(AiNodeGetArray(node, vidxsStr)));
                }
            }
        } else if (typeName == SdfValueTypeNames->Color3f || typeName == SdfValueTypeNames->Color3fArray)
            primvarType = AI_TYPE_RGB;
        else if (typeName == SdfValueTypeNames->Color4f || typeName == SdfValueTypeNames->Color4fArray)
            primvarType = AI_TYPE_RGBA;
        else if (typeName == SdfValueTypeNames->Float || typeName == SdfValueTypeNames->FloatArray)
            primvarType = AI_TYPE_FLOAT;
        else if (typeName == SdfValueTypeNames->Int || typeName == SdfValueTypeNames->IntArray)
            primvarType = AI_TYPE_INT;
        else if (typeName == SdfValueTypeNames->UInt || typeName == SdfValueTypeNames->UIntArray)
            primvarType = AI_TYPE_UINT;
        else if (typeName == SdfValueTypeNames->UChar || typeName == SdfValueTypeNames->UCharArray)
            primvarType = AI_TYPE_BYTE;
        else if (typeName == SdfValueTypeNames->Bool || typeName == SdfValueTypeNames->BoolArray)
            primvarType = AI_TYPE_BOOLEAN;
        else if (typeName == SdfValueTypeNames->String || typeName == SdfValueTypeNames->StringArray) 
            primvarType = AI_TYPE_STRING;
        
        if (primvarType == AI_TYPE_NONE)
            continue;
        
        declaration += AiParamGetTypeName(primvarType);

        // Declare a user-defined parameter, only if it doesn't already exist
        if (AiNodeEntryLookUpParameter(nodeEntry, AtString(arnoldName.GetText())) == nullptr) {
            AiNodeDeclare(node, arnoldName.GetText(), declaration.c_str());
        }

        bool hasIdxs = false;

        // If the primvar is indexed, we need to set this as a 
        if (interpolation == UsdGeomTokens->faceVarying) {
            VtIntArray vtIndices;
            std::vector<unsigned int> indexes;
                
            if (primvar.IsIndexed() && primvar.GetIndices(&vtIndices, frame)
                     && !vtIndices.empty()) {
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
                // so we need to get the VtValue just to find its size
                VtValue tmp;
                primvar.Get(&tmp);
                indexes.resize(tmp.GetArraySize());
                // Fill it with 0, 1, ..., 99.
                std::iota(std::begin(indexes), std::end(indexes), 0);
            }
            if (indexes.empty())
                continue;
            
            // If the mesh has left-handed orientation, we need to invert the
            // indices of primvars for each face
            if (orientation)
                orientation->orientFaceIndexAttribute(indexes);

            AiNodeSetArray(node, arnoldIndexName.c_str(), 
                AiArrayConvert(indexes.size(), 1, AI_TYPE_UINT, indexes.data()));
            
            hasIdxs = true;
        }
        int arrayType = AI_TYPE_NONE;        
        if (interpolation != UsdGeomTokens->constant && 
                primvarType != AI_TYPE_ARRAY) {
            arrayType = primvarType;
            primvarType = AI_TYPE_ARRAY;
        }

        bool animated = time.motion_blur && primvar.ValueMightBeTimeVarying();
        
        InputAttribute inputAttr(primvar);
        //inputAttr.attr = (UsdAttribute*)&attr;
        inputAttr.computeFlattened = (interpolation != UsdGeomTokens->constant && !hasIdxs);
        
        readAttribute(inputAttr,  node, arnoldName.GetText(), time, context, 
            primvarType, arrayType);

    }
}
