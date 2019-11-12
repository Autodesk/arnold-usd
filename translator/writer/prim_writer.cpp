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
#include "prim_writer.h"

#include <ai.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
inline GfMatrix4d NodeGetGfMatrix(const AtNode* node, const char* param)
{
    const AtMatrix mat = AiNodeGetMatrix(node, param);
    GfMatrix4f matFlt(mat.data);
    return GfMatrix4d(matFlt);
};

inline const char* GetEnum(AtEnum en, int32_t id)
{
    if (en == nullptr) {
        return "";
    }
    if (id < 0) {
        return "";
    }
    for (auto i = 0; i <= id; ++i) {
        if (en[i] == nullptr) {
            return "";
        }
    }
    return en[id];
}

// Helper to convert parameters. It's a unordered_map having the attribute type
// as the key, and as value the USD attribute type, as well as the function
// doing the conversion. Note that this could be switched to a vector instead of
// a map for efficiency, but for now having it this way makes the code simpler
using ParamConversionMap = std::unordered_map<uint8_t, UsdArnoldPrimWriter::ParamConversion>;
const ParamConversionMap& _ParamConversionMap()
{
    static const ParamConversionMap ret = {
    {AI_TYPE_BYTE,
     {SdfValueTypeNames->UChar,
      [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetByte(no, na)); },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->BYTE() == AiNodeGetByte(no, na));
      }}},
    {AI_TYPE_INT,
     {SdfValueTypeNames->Int, [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetInt(no, na)); },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->INT() == AiNodeGetInt(no, na));
      }}},
    {AI_TYPE_UINT,
     {SdfValueTypeNames->UInt,
      [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetUInt(no, na)); },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->UINT() == AiNodeGetUInt(no, na));
      }}},
    {AI_TYPE_BOOLEAN,
     {SdfValueTypeNames->Bool,
      [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetBool(no, na)); },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->BOOL() == AiNodeGetBool(no, na));
      }}},
    {AI_TYPE_FLOAT,
     {SdfValueTypeNames->Float,
      [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetFlt(no, na)); },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->FLT() == AiNodeGetFlt(no, na));
      }}},
    {AI_TYPE_RGB,
     {SdfValueTypeNames->Color3f,
      [](const AtNode* no, const char* na) -> VtValue {
          const auto v = AiNodeGetRGB(no, na);
          return VtValue(GfVec3f(v.r, v.g, v.b));
      },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->RGB() == AiNodeGetRGB(no, na));
      }}},
    {AI_TYPE_RGBA,
     {SdfValueTypeNames->Color4f,
      [](const AtNode* no, const char* na) -> VtValue {
          const auto v = AiNodeGetRGBA(no, na);
          return VtValue(GfVec4f(v.r, v.g, v.b, v.a));
      },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->RGBA() == AiNodeGetRGBA(no, na));
      }}},
    {AI_TYPE_VECTOR,
     {SdfValueTypeNames->Vector3f,
      [](const AtNode* no, const char* na) -> VtValue {
          const auto v = AiNodeGetVec(no, na);
          return VtValue(GfVec3f(v.x, v.y, v.z));
      },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->VEC() == AiNodeGetVec(no, na));
      }}},
    {AI_TYPE_VECTOR2,
     {SdfValueTypeNames->Float2,
      [](const AtNode* no, const char* na) -> VtValue {
          const auto v = AiNodeGetVec2(no, na);
          return VtValue(GfVec2f(v.x, v.y));
      },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->VEC2() == AiNodeGetVec2(no, na));
      }}},
    {AI_TYPE_STRING,
     {SdfValueTypeNames->String,
      [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetStr(no, na).c_str()); },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->STR() == AiNodeGetStr(no, na));
      }}},
    {AI_TYPE_POINTER, {SdfValueTypeNames->String, nullptr, nullptr}},
    {AI_TYPE_NODE,
     {SdfValueTypeNames->String,
      [](const AtNode* no, const char* na) -> VtValue {
          std::string targetName;
          AtNode* target = (AtNode*)AiNodeGetPtr(no, na);
          if (target) {
              targetName = AiNodeGetName(target);
          }
          return VtValue(targetName);
      },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (AiNodeGetPtr(no, na) != nullptr);
      }}},
    {AI_TYPE_MATRIX,
     {SdfValueTypeNames->Matrix4d,
      [](const AtNode* no, const char* na) -> VtValue { return VtValue(NodeGetGfMatrix(no, na)); },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return AiM4IsIdentity(AiNodeGetMatrix(no, na));
      }}},
    {AI_TYPE_ENUM,
     {SdfValueTypeNames->String,
      [](const AtNode* no, const char* na) -> VtValue {
          const auto* nentry = AiNodeGetNodeEntry(no);
          if (nentry == nullptr) {
              return VtValue("");
          }
          const auto* pentry = AiNodeEntryLookUpParameter(nentry, na);
          if (pentry == nullptr) {
              return VtValue("");
          }
          const auto enums = AiParamGetEnum(pentry);
          return VtValue(GetEnum(enums, AiNodeGetInt(no, na)));
      },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->INT() == AiNodeGetInt(no, na));
      }}},
    {AI_TYPE_CLOSURE, {SdfValueTypeNames->String, nullptr, nullptr}},
    {AI_TYPE_USHORT,
     {SdfValueTypeNames->UInt,
      [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetUInt(no, na)); },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->UINT() == AiNodeGetUInt(no, na));
      }}},
    {AI_TYPE_HALF,
     {SdfValueTypeNames->Half,
      [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetFlt(no, na)); },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (pentry->FLT() == AiNodeGetFlt(no, na));
      }}}};
    return ret;
}

} // namespace

// Get the conversion item for this node type (see above)
const UsdArnoldPrimWriter::ParamConversion* UsdArnoldPrimWriter::getParamConversion(uint8_t type)
{
    const auto it = _ParamConversionMap().find(type);
    if (it != _ParamConversionMap().end()) {
        return &it->second;
    } else {
        return nullptr;
    }
}

/**
 *    Get the USD node name for this Arnold node. We need to replace the
 *forbidden characters from the names. Also, we must ensure that the first
 *character is a slash
 **/
std::string UsdArnoldPrimWriter::GetArnoldNodeName(const AtNode* node)
{
    std::string name = AiNodeGetName(node);
    if (name.empty()) {
        return name;
    }

    // We need to determine which parameters must be converted to underscores
    // and which must be converted to slashes. In Maya node names, pipes
    // correspond to hierarchies so for now I'm converting them to slashes.
    std::replace(name.begin(), name.end(), '|', '/');
    std::replace(name.begin(), name.end(), '@', '_');
    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ':', '_');

    if (name[0] != '/') {
        name = std::string("/") + name;
    }

    return name;
}

void UsdArnoldPrimWriter::writeArnoldParameters(
    const AtNode* node, UsdArnoldWriter& writer, UsdPrim& prim, bool use_namespace)
{
    // Loop over the arnold parameters, and write them
    const AtNodeEntry* nodeEntry = AiNodeGetNodeEntry(node);
    AtParamIterator* paramIter = AiNodeEntryGetParamIterator(nodeEntry);

    while (!AiParamIteratorFinished(paramIter)) {
        const AtParamEntry* paramEntry = AiParamIteratorGetNext(paramIter);
        const char* paramName(AiParamGetName(paramEntry));
        if (strcmp(paramName, "name") == 0) { // "name" is an exception and shouldn't be saved
            continue;
        }

        int paramType = AiParamGetType(paramEntry);
        // for now all attributes are in the "arnold:" namespace
        std::string usdParamName =
            (use_namespace) ? std::string("arnold:") + std::string(paramName) : std::string(paramName);

        if (paramType == AI_TYPE_ARRAY) {
            AtArray* array = AiNodeGetArray(node, paramName);
            if (array == nullptr) {
                continue;
            }
            int arrayType = AiArrayGetType(array);
            unsigned int arraySize = AiArrayGetNumElements(array);
            if (arraySize == 0) {
                continue; // no need to export empty arrays
            }

            UsdAttribute attr;
            SdfValueTypeName usdTypeName;

            switch (arrayType) {
                {
                    case AI_TYPE_BYTE:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->UCharArray, false);
                        VtArray<unsigned char> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            vtArr[i] = AiArrayGetByte(array, i);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_INT:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->IntArray, false);
                        VtArray<int> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            vtArr[i] = AiArrayGetInt(array, i);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_UINT:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->UIntArray, false);
                        VtArray<unsigned int> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            vtArr[i] = AiArrayGetUInt(array, i);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_BOOLEAN:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->BoolArray, false);
                        VtArray<bool> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            vtArr[i] = AiArrayGetBool(array, i);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_FLOAT:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->FloatArray, false);
                        VtArray<float> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            vtArr[i] = AiArrayGetFlt(array, i);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_RGB:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->Color3fArray, false);
                        VtArray<GfVec3f> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            AtRGB col = AiArrayGetRGB(array, i);
                            vtArr[i] = GfVec3f(col.r, col.g, col.b);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_VECTOR:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->Vector3fArray, false);
                        VtArray<GfVec3f> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            AtVector vec = AiArrayGetVec(array, i);
                            vtArr[i] = GfVec3f(vec.x, vec.y, vec.z);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_RGBA:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->Color4fArray, false);
                        VtArray<GfVec4f> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            AtRGBA col = AiArrayGetRGBA(array, i);
                            vtArr[i] = GfVec4f(col.r, col.g, col.b, col.a);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_VECTOR2:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->Float2Array, false);
                        VtArray<GfVec2f> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            AtVector2 vec = AiArrayGetVec2(array, i);
                            vtArr[i] = GfVec2f(vec.x, vec.y);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_STRING:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->StringArray, false);
                        VtArray<std::string> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            AtString str = AiArrayGetStr(array, i);
                            vtArr[i] = str.c_str();
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_MATRIX:
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->Matrix4dArray, false);
                        VtArray<GfMatrix4d> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            const AtMatrix mat = AiArrayGetMtx(array, i);
                            GfMatrix4f matFlt(mat.data);
                            vtArr[i] = GfMatrix4d(matFlt);
                        }
                        attr.Set(vtArr);
                        break;
                }
                {
                    case AI_TYPE_NODE:
                        // only export the first element for now
                        attr = prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->StringArray, false);
                        VtArray<std::string> vtArr(arraySize);
                        for (unsigned int i = 0; i < arraySize; ++i) {
                            AtNode* target = (AtNode*)AiArrayGetPtr(array, i);
                            vtArr[i] = (target) ? AiNodeGetName(target) : "";
                        }
                        attr.Set(vtArr);
                }
            }
        } else {
            const auto iterType = getParamConversion(paramType);

            bool isLinked = AiNodeIsLinked(node, paramName);

            if (!isLinked && iterType != nullptr && iterType->d(node, paramName, AiParamGetDefault(paramEntry))) {
                continue; // default value, no need to write it
            }

            UsdAttribute attr = prim.CreateAttribute(TfToken(usdParamName), iterType->type, false);
            if (iterType != nullptr && iterType->f != nullptr) {
                attr.Set(iterType->f(node, paramName));
            }
            if (isLinked) {
                AtNode* target = AiNodeGetLink(node, paramName);
                if (target) {
                    writer.writePrimitive(target);
                    attr.AddConnection(SdfPath(GetArnoldNodeName(target)));
                }
            }
        }
    }
    AiParamIteratorDestroy(paramIter);
}
//=================  Unsupported primitives
/**
 *   This function will be invoked for node types that are explicitely not
 *supported. We'll dump a warning, saying that this node isn't supported in the
 *USD conversion
 **/
void UsdArnoldWriteUnsupported::write(const AtNode* node, UsdArnoldWriter& writer)
{
    if (node == NULL) {
        return;
    }

    AiMsgWarning("UsdArnoldWriter : %s nodes not supported, cannot write %s", _type.c_str(), AiNodeGetName(node));
}
