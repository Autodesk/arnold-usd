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
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
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
    {AI_TYPE_POINTER, {SdfValueTypeNames->String, nullptr, // TODO: how should we write pointer attributes ??
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (AiNodeGetPtr(no, na) == nullptr);
      }}},
    {AI_TYPE_NODE,
     {SdfValueTypeNames->String,
      [](const AtNode* no, const char* na) -> VtValue {
          std::string targetName;
          AtNode* target = (AtNode*)AiNodeGetPtr(no, na);
          if (target) {
              targetName = UsdArnoldPrimWriter::getArnoldNodeName(target);
          }
          return VtValue(targetName);
      },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return (AiNodeGetPtr(no, na) == nullptr);
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
      {AI_TYPE_CLOSURE, {SdfValueTypeNames->String,
      [](const AtNode* no, const char* na) -> VtValue {
          return VtValue("");
      },
      [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
          return true;
      }}},
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

/** 
 *  UsdArnoldBuiltinParamWriter handles the conversion of USD builtin attributes.
 *  These attributes already exist in the USD prim schemas so we just need to set 
 *  their value
 **/
class UsdArnoldBuiltinParamWriter
{
public:
    UsdArnoldBuiltinParamWriter(const AtNode *node, UsdPrim &prim, const AtParamEntry *paramEntry, const UsdAttribute &attr) : 
                    _node(node),
                    _prim(prim),
                    _paramEntry(paramEntry), 
                    _attr(attr) {}

    uint8_t getParamType() const {return AiParamGetType(_paramEntry);}
    bool skipDefaultValue(const UsdArnoldPrimWriter::ParamConversion *paramConversion) const {return false;}
    AtString getParamName() const {return AiParamGetName(_paramEntry);}


    template <typename T>
    void ProcessAttribute(const SdfValueTypeName &typeName, const T &value)
    {
        // The UsdAttribute already exists, we just need to set it
        _attr.Set(value);
    }
    template <typename T>
    void ProcessAttributeKeys(const SdfValueTypeName &typeName, const std::vector<T> &values, 
        float motionStart, float motionEnd)
    {
        if (values.empty())
            return;

        if (values.size() == 1)
            _attr.Set(values[0]);
        else {
            
            if (motionStart >= motionEnd) {
                // invalid motion start / end points, let's just write a single value
                _attr.Set(values[0]);
            } else {
                float motionDelta = (motionEnd - motionStart) / ((int) values.size() - 1);
                float time = motionStart;
                for (size_t i = 0; i < values.size(); ++i, time += motionDelta) {
                    _attr.Set(values[i], UsdTimeCode(time));
                }
            }
        }
        
    }
    void AddConnection(const SdfPath& path) {
        _attr.AddConnection(path);
    }

private:
    const AtNode *_node;
    UsdPrim &_prim;
    const AtParamEntry *_paramEntry;
    UsdAttribute _attr;
};

/** 
 *  UsdArnoldCustomParamWriter handles the conversion of arnold-specific attributes,
 *  that do not exist in the USD native schemas. We need to create them with the right type, 
 *  prefixing them with the "arnold:" namespace, and then we set their value.
 **/
class UsdArnoldCustomParamWriter
{
public:
    UsdArnoldCustomParamWriter(const AtNode *node, UsdPrim &prim, const AtParamEntry *paramEntry, const std::string &scope) : 
                _node(node),
                _prim(prim),
                _paramEntry(paramEntry), 
                _scope(scope){}
    uint8_t getParamType() const {return AiParamGetType(_paramEntry);}
    bool skipDefaultValue(const UsdArnoldPrimWriter::ParamConversion *paramConversion) const {
        AtString paramNameStr = getParamName();
        return paramConversion && paramConversion->d && paramConversion->d(_node, paramNameStr.c_str(), AiParamGetDefault(_paramEntry));
    }
    AtString getParamName() const {return AiParamGetName(_paramEntry);}

    template <typename T>
    void ProcessAttribute(const SdfValueTypeName &typeName, T &value) {
        // Create the UsdAttribute, in the desired scope, and set its value
        AtString paramNameStr = getParamName();
        std::string paramName(paramNameStr.c_str());
        std::string usdParamName = (_scope.empty()) ? paramName : _scope + std::string(":") + paramName;
        _attr = _prim.CreateAttribute(TfToken(usdParamName), typeName, false);
        _attr.Set(value);
    }
    template <typename T>
    void ProcessAttributeKeys(const SdfValueTypeName &typeName, const std::vector<T> &values, 
        float motionStart, float motionEnd)
    {
        if (values.empty())
            return;

        if (values.size() == 1) {
            ProcessAttribute(typeName, values[0]);
            return;
        }
        // Create the UsdAttribute, in the desired scope, and set its value
        AtString paramNameStr = getParamName();
        std::string paramName(paramNameStr.c_str());
        std::string usdParamName = (_scope.empty()) ? paramName : _scope + std::string(":") + paramName;
        _attr = _prim.CreateAttribute(TfToken(usdParamName), typeName, false);

        if (motionStart >= motionEnd) {
            // invalid motion start / end points, let's just write a single value
            _attr.Set(values[0]);
        } else {
            float motionDelta = (motionEnd - motionStart) / ((int) values.size() - 1);
            float time = motionStart;
            for (size_t i = 0; i < values.size(); ++i, time += motionDelta) {
                _attr.Set(values[i], UsdTimeCode(time));
            }
        }
    }
    void AddConnection(const SdfPath& path) {
        _attr.AddConnection(path);
    }

private:
    const AtNode *_node;
    UsdPrim &_prim;
    const AtParamEntry *_paramEntry;
    std::string _scope;
    UsdAttribute _attr;
    
};

/** 
 *  UsdArnoldPrimvarWriter handles the conversion of arnold user data, that must
 *  be exported as USD primvars (without any namespace). 
 **/
class UsdArnoldPrimvarWriter
{
public:
    UsdArnoldPrimvarWriter(const AtNode *node, UsdPrim &prim, const AtUserParamEntry *userParamEntry) :
                        _node(node),
                        _prim(prim),
                        _userParamEntry(userParamEntry),
                        _primvarsAPI(prim) {}

    bool skipDefaultValue(const UsdArnoldPrimWriter::ParamConversion *paramConversion) const {return false;}
    uint8_t getParamType() const {
       // The definition of user data in arnold is a bit different from primvars in USD :
       // For indexed, varying, uniform user data that are of type i.e. Vector, we will 
       // actually have an array of vectors (1 per-vertex / per-face / or per-face-vertex).
       // On the other hand, in USD, in this case the primvar will be of type VectorArray,
       // and it doesn't make any sense for such primvars to be of a non-array type.

       // So first, we check the category of the arnold user data, 
       // and if it's constant we return the actual user data type
        if (AiUserParamGetCategory(_userParamEntry) == AI_USERDEF_CONSTANT)
            return AiUserParamGetType(_userParamEntry);

      // Otherwise, for varying / uniform / indexed, the type must actually be an array.
        return AI_TYPE_ARRAY;
    }
    
    AtString getParamName() const {return AtString(AiUserParamGetName(_userParamEntry));}
    
    template <typename T>
    void ProcessAttribute(const SdfValueTypeName &typeName, T &value) {
        SdfValueTypeName type = typeName;

        uint8_t paramType = getParamType();
        TfToken category;
        AtString paramNameStr = getParamName();
        const char *paramName = paramNameStr.c_str();
        switch (AiUserParamGetCategory(_userParamEntry)) {
            case AI_USERDEF_UNIFORM:
                category = UsdGeomTokens->uniform;
            break;
            case AI_USERDEF_VARYING:
                category = UsdGeomTokens->varying;
            break;
            case AI_USERDEF_INDEXED:
                category = UsdGeomTokens->faceVarying;
            break;
            case AI_USERDEF_CONSTANT:
            default:
                category = UsdGeomTokens->constant;
        }
        unsigned int elementSize = (paramType == AI_TYPE_ARRAY) ? AiArrayGetNumElements(AiNodeGetArray(_node, paramName)) : 1;
        
        // Special case for displayColor, that needs to be set as a color array
        static AtString displayColorStr("displayColor");
        if (paramNameStr == displayColorStr && type == SdfValueTypeNames->Color3f) {
            if (std::is_same<T, VtValue>::value) {
                VtValue *vtVal = (VtValue*)(&value);
                VtArray<GfVec3f> arrayValue;
                arrayValue.push_back(vtVal->Get<GfVec3f>());
                UsdGeomPrimvar primVar = _primvarsAPI.GetPrimvar(TfToken("displayColor"));
                if (primVar)
                    primVar.Set(arrayValue);
            }
            return;
        }
        // Same for displayOpacity, as a float array
        static AtString displayOpacityStr("displayOpacity");
        if (paramNameStr == displayOpacityStr && type == SdfValueTypeNames->Float) {
            if (std::is_same<T, VtValue>::value) {
                VtValue *vtVal = (VtValue*)(&value);
                VtArray<float> arrayValue;
                arrayValue.push_back(vtVal->Get<float>());
                UsdGeomPrimvar primVar = _primvarsAPI.GetPrimvar(TfToken("displayOpacity"));
                if (primVar)
                    primVar.Set(arrayValue);
            }
            return;
        } 

        UsdGeomPrimvar primVar = _primvarsAPI.CreatePrimvar(TfToken(paramName),
                                    type,
                                    category,
                                    elementSize);
        primVar.Set(value);

        if (category == UsdGeomTokens->faceVarying) {
            // in case of indexed user data, we need to find the arnold array with an "idxs" suffix 
            // (arnold convention), and set it as the primVar indices
            std::string indexAttr = std::string(paramNameStr.c_str()) + std::string("idxs");
            AtString indexAttrStr(indexAttr.c_str());
            AtArray *indexArray = AiNodeGetArray(_node, indexAttrStr);
            unsigned int indexArraySize = (indexArray) ? AiArrayGetNumElements(indexArray) : 0;
            if (indexArraySize > 0) {
                VtIntArray vtIndices(indexArraySize);
                for (unsigned int i = 0; i < indexArraySize; ++i) {
                    vtIndices[i] = AiArrayGetInt(indexArray, i);
                }
                primVar.SetIndices(vtIndices);
            }
        }        
    }
    template <typename T>
    void ProcessAttributeKeys(const SdfValueTypeName &typeName, const std::vector<T> &values, 
        float motionStart, float motionEnd)
    {
        if (!values.empty())
            ProcessAttribute(typeName, values[0]);
        // we're currently not supporting motion blur in primvars
    }

    void AddConnection(const SdfPath& path) {} // cannot set connections on primvars

private:
    const AtNode *_node;
    UsdPrim &_prim;
    const AtUserParamEntry *_userParamEntry;
    UsdGeomPrimvarsAPI _primvarsAPI;
    
};

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
 *    Function invoked from the UsdArnoldWriter that exports an input arnold node to USD
 **/ 
void UsdArnoldPrimWriter::writeNode(const AtNode *node, UsdArnoldWriter &writer)
{
    // we're exporting a new node, so we store the previous list of exported 
    // attributes and we clear it for the new node being written
    decltype(_exportedAttrs) prevExportedAttrs;
    prevExportedAttrs.swap(_exportedAttrs);

    _motionStart = (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), "motion_start")) ?
        AiNodeGetFlt(node, "motion_start") : writer.getShutterStart();
    _motionEnd = (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), "motion_end")) ?
        AiNodeGetFlt(node, "motion_end") : writer.getShutterEnd();            

    // Now call the virtual function write() defined for each primitive type
    write(node, writer); 

    // restore the previous list (likely empty, unless there have been recursive creation of nodes)
    _exportedAttrs.swap(prevExportedAttrs);
}
/**
 *    Get the USD node name for this Arnold node. We need to replace the
 *forbidden characters from the names. Also, we must ensure that the first
 *character is a slash
 **/
std::string UsdArnoldPrimWriter::getArnoldNodeName(const AtNode* node)
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

// Ensure a connected node is properly translated, handle the output attributes, 
// and return its name
static inline std::string GetConnectedNode(UsdArnoldWriter &writer, AtNode *target, int outComp = -1)
{
    // First, ensure the primitive was written to usd
    writer.writePrimitive(target);
    
    // Get the usd name of this prim
    std::string targetName = UsdArnoldPrimWriter::getArnoldNodeName(target); 
    UsdPrim targetPrim = writer.getUsdStage()->GetPrimAtPath(SdfPath(targetName));
    
    // ensure the prim exists for the link
    if (!targetPrim)
        return std::string();

    // check the output type of this node
    int targetEntryType = AiNodeEntryGetOutputType(AiNodeGetNodeEntry(target));
    if (outComp < 0) { // Connection on the full node output
        SdfValueTypeName type;
        const auto outputIterType = UsdArnoldPrimWriter::getParamConversion(targetEntryType);
        if (outputIterType) {
            // Create the output attribute on the node, of the corresponding type
            // For now we call it outputs:out to be generic, but it could be called rgb, vec, float, etc...
            UsdAttribute attr = targetPrim.CreateAttribute(TfToken("outputs:out"), outputIterType->type, false);
            // the connection will point at this output attribute
            targetName += ".outputs:out";
        } 
    } else { // connection on an output component (r, g, b, etc...)
        std::string compList;
        // we support components on vectors and colors only, and they're 
        // always represented by a single character. 
        // This string contains the sequence for each of these characters
        switch(targetEntryType) {
            case AI_TYPE_VECTOR2:
               compList = "xy";
               break;
            case AI_TYPE_VECTOR:
               compList = "xyz";
               break;
            case AI_TYPE_RGB:
               compList = "rgb";
               break;
            case AI_TYPE_RGBA:
               compList = "rgba";
               break;
        }
        if (outComp < (int)compList.length()) {
            // Let's create the output attribute for this component.
            // As of now, these components are always float
            std::string outName = std::string("outputs:") + std::string(1, compList[outComp]);
            UsdAttribute attr = targetPrim.CreateAttribute(TfToken(outName), SdfValueTypeNames->Float, false);
            targetName += std::string(".") + outName;
        }
    }
    return targetName;
}
/** 
 *   Internal function to convert an arnold attribute to USD, whether it's an existing UsdAttribute or 
 *   a custom one that we need to create. 
 *   @param attrWriter Template class that can either be a UsdArnoldBuiltinParamWriter (for USD builtin attributes), 
 *                     or a UsdArnoldCustomParamWriter (for arnold-specific attributes), 
 *                     or UsdArnoldPrimvarWriter (for arnold user data)
 **/
template <typename T>
static inline bool convertArnoldAttribute(const AtNode *node, UsdPrim &prim, UsdArnoldWriter &writer, 
    UsdArnoldPrimWriter &primWriter, T& attrWriter)
{
    int paramType = attrWriter.getParamType();
    const char* paramName = attrWriter.getParamName();
    
    if (paramType == AI_TYPE_ARRAY) {
        AtArray* array = AiNodeGetArray(node, paramName);
        if (array == nullptr) {
            return false;
        }
        int arrayType = AiArrayGetType(array);
        unsigned int numElements = AiArrayGetNumElements(array);
        if (numElements == 0) {
            return false;
        }
        unsigned int numKeys = AiArrayGetNumKeys(array);
        float motionStart = primWriter.getMotionStart();
        float motionEnd = primWriter.getMotionEnd();

        SdfValueTypeName usdTypeName;
        int index = 0;
        switch (arrayType) {
            {
                case AI_TYPE_BYTE:                    
                    std::vector<VtArray<unsigned char> > vtMotionArray(numKeys);
                    unsigned char *arrayMap = (unsigned char *) AiArrayMap(array);
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<unsigned char> &vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        memcpy(&vtArr[0], &arrayMap[j*numElements], numElements * sizeof(unsigned char));
                    }
                        
                    attrWriter.ProcessAttributeKeys(SdfValueTypeNames->UCharArray, vtMotionArray, 
                        motionStart, motionEnd);
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_INT:
                    std::vector<VtArray<int> > vtMotionArray(numKeys);
                    int *arrayMap = (int *) AiArrayMap(array);
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<int> &vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        memcpy(&vtArr[0], &arrayMap[j*numElements], numElements * sizeof(int));
                    }                        
                    attrWriter.ProcessAttributeKeys(SdfValueTypeNames->IntArray, vtMotionArray,
                        motionStart, motionEnd);
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_UINT:
                    std::vector<VtArray<unsigned int> > vtMotionArray(numKeys);
                    unsigned int *arrayMap = (unsigned int *) AiArrayMap(array);
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<unsigned int> &vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        memcpy(&vtArr[0], &arrayMap[j*numElements], numElements * sizeof(unsigned int));
                    }                        
                    attrWriter.ProcessAttributeKeys(SdfValueTypeNames->UIntArray, vtMotionArray,
                        motionStart, motionEnd);
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_BOOLEAN:
                    std::vector<VtArray<bool> > vtMotionArray(numKeys);
                    bool *arrayMap = (bool *) AiArrayMap(array);
                    int index = 0;
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<bool> &vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        memcpy(&vtArr[0], &arrayMap[j*numElements], numElements * sizeof(bool));
                    }                        
                    attrWriter.ProcessAttributeKeys(SdfValueTypeNames->BoolArray, vtMotionArray,
                        motionStart, motionEnd);
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_FLOAT:
                    std::vector<VtArray<float> > vtMotionArray(numKeys);
                    float *arrayMap = (float *) AiArrayMap(array);
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<float> &vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        memcpy(&vtArr[0], &arrayMap[j*numElements], numElements * sizeof(float));
                    }                        
                    attrWriter.ProcessAttributeKeys(SdfValueTypeNames->FloatArray, vtMotionArray,
                        motionStart, motionEnd);
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_RGB:
                    std::vector<VtArray<GfVec3f> > vtMotionArray(numKeys);
                    GfVec3f *arrayMap = (GfVec3f *) AiArrayMap(array);
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<GfVec3f> &vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        memcpy(&vtArr[0], &arrayMap[j*numElements], numElements * sizeof(GfVec3f));
                    }                        
                    attrWriter.ProcessAttributeKeys(SdfValueTypeNames->Color3fArray, vtMotionArray,
                        motionStart, motionEnd);
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_VECTOR:
                    std::vector<VtArray<GfVec3f> > vtMotionArray(numKeys);
                    GfVec3f *arrayMap = (GfVec3f *) AiArrayMap(array);
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<GfVec3f> &vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        memcpy(&vtArr[0], &arrayMap[j*numElements], numElements * sizeof(GfVec3f));
                    }
                    attrWriter.ProcessAttributeKeys(SdfValueTypeNames->Vector3fArray, vtMotionArray,
                        motionStart, motionEnd);
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_RGBA:
                    std::vector<VtArray<GfVec4f> > vtMotionArray(numKeys);
                    GfVec4f *arrayMap = (GfVec4f *) AiArrayMap(array);
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<GfVec4f> &vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        memcpy(&vtArr[0], &arrayMap[j*numElements], numElements * sizeof(GfVec4f));
                    }
                    attrWriter.ProcessAttributeKeys(SdfValueTypeNames->Color4fArray, vtMotionArray, 
                        motionStart, motionEnd);
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_VECTOR2:
                    std::vector<VtArray<GfVec2f> > vtMotionArray(numKeys);
                    GfVec2f *arrayMap = (GfVec2f *) AiArrayMap(array);
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<GfVec2f> &vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        memcpy(&vtArr[0], &arrayMap[j*numElements], numElements * sizeof(GfVec2f));
                    }
                    attrWriter.ProcessAttributeKeys(SdfValueTypeNames->Float2Array, vtMotionArray,
                        motionStart, motionEnd);
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_STRING:
                    // No animation for string arrays
                    VtArray<std::string> vtArr(numElements);
                    for (unsigned int i = 0; i < numElements; ++i) {
                        AtString str = AiArrayGetStr(array, i);
                        vtArr[i] = str.c_str();
                    }
                    attrWriter.ProcessAttribute(SdfValueTypeNames->StringArray, vtArr);
                    break;
            }
            {
                case AI_TYPE_MATRIX:
                    std::vector<VtArray<GfMatrix4d> > vtMotionArray(numKeys);
                    AtMatrix *arrayMap = (AtMatrix *) AiArrayMap(array);
                    if (arrayMap) {
                        int index = 0;
                        for (unsigned int j = 0; j < numKeys; ++j) {
                            VtArray<GfMatrix4d> &vtArr = vtMotionArray[j];
                            vtArr.resize(numElements);
                            for (unsigned int i = 0; i < numElements; ++i, ++index) {
                                const AtMatrix mat = arrayMap[index];
                                GfMatrix4f matFlt(mat.data);
                                vtArr[i] = GfMatrix4d(matFlt);
                            }
                        }
                        attrWriter.ProcessAttributeKeys(SdfValueTypeNames->Matrix4dArray, vtMotionArray,
                            motionStart, motionEnd);
                    }
                    AiArrayUnmap(array);
                    break;
            }
            {
                case AI_TYPE_NODE:
                    // only export the first element for now
                    VtArray<std::string> vtArr(numElements);
                    for (unsigned int i = 0; i < numElements; ++i) {
                        AtNode* target = (AtNode*)AiArrayGetPtr(array, i);
                        vtArr[i] = (target) ? UsdArnoldPrimWriter::getArnoldNodeName(target) : "";
                    }
                    attrWriter.ProcessAttribute(SdfValueTypeNames->StringArray, vtArr);
            }
        }
    } else {
        const auto iterType = UsdArnoldPrimWriter::getParamConversion(paramType);
        bool isLinked = AiNodeIsLinked(node, paramName);
        if (!isLinked && attrWriter.skipDefaultValue(iterType)) {
            return false;
        }
        if (iterType != nullptr && iterType->f != nullptr)
        {
            VtValue value = iterType->f(node, paramName);
            attrWriter.ProcessAttribute(iterType->type, value);
        }

        if (isLinked) {
            int outComp = -1;
            AtNode* target = AiNodeGetLink(node, paramName, &outComp);
            // Get the link on the arnold node
            if (target) {
                std::string targetName = GetConnectedNode(writer, target, outComp); 
                // Process the connection
                if (!targetName.empty())
                    attrWriter.AddConnection(SdfPath(targetName));
            } else {
                // we get here if there are link on component channels (.r, .y, etc...)
                // => AiNodeIsLinked returns true but AiNodeGetLink is empty.
                // Here we want to insert an "adapter" shader between the attribute and the
                // link target. This adapter can always be float_to_rgba, independantly of
                // the attribute type, because arnold supports links of different types.
                std::string adapterName = prim.GetPath().GetText();
                adapterName += std::string("_") + std::string(paramName);
                UsdShadeShader shaderAPI = UsdShadeShader::Define(writer.getUsdStage(), SdfPath(adapterName));
                // float_to_rgba can be used to convert rgb, rgba, vector, and vector2                
                shaderAPI.CreateIdAttr().Set(TfToken("arnold:float_to_rgba"));
                // connect the attribute to the adapter
                attrWriter.AddConnection(SdfPath(adapterName));

                UsdAttribute attributes[4];
                float defaultValues[4] = {0.f, 0.f, 0.f, 1.f};
                std::string attrNames[4] = {"inputs:r", "inputs:g", "inputs:b", "inputs:a"};
                for (unsigned int i = 0; i < 4; ++i) {
                    attributes[i] = shaderAPI.GetPrim().CreateAttribute(TfToken(attrNames[i]), SdfValueTypeNames->Float, false);
                    attributes[i].Set(defaultValues[i]);
                }
                float attrValues[4] = {0.f, 0.f, 0.f, 0.f};
                std::vector<std::string> channels(4);
                switch (paramType) {
                    {
                    case AI_TYPE_VECTOR:
                        channels[0] = ".x";
                        channels[1] = ".y";
                        channels[2] = ".z";
                        AtVector vec = AiNodeGetVec(node, paramName);
                        attrValues[0] = vec.x;
                        attrValues[1] = vec.y;
                        attrValues[2] = vec.z;
                        break;
                    }
                    {
                    case AI_TYPE_VECTOR2:
                        channels[0] = ".x";
                        channels[1] = ".y";                        
                        AtVector2 vec = AiNodeGetVec2(node, paramName);
                        attrValues[0] = vec.x;
                        attrValues[1] = vec.y;
                        break;
                    }
                    {
                    case AI_TYPE_RGBA:
                        channels[0] = ".r";
                        channels[1] = ".g";
                        channels[2] = ".b";
                        channels[3] = ".a";
                        AtRGBA col = AiNodeGetRGBA(node, paramName);
                        attrValues[0] = col.r;
                        attrValues[1] = col.g;
                        attrValues[2] = col.b;
                        attrValues[3] = col.a;
                        break;
                    }
                    {
                    case AI_TYPE_RGB:
                        channels[0] = ".r";
                        channels[1] = ".g";
                        channels[2] = ".b";
                        AtRGB col = AiNodeGetRGB(node, paramName);
                        attrValues[0] = col.r;
                        attrValues[1] = col.g;
                        attrValues[2] = col.b;
                        break;
                    }
                    default:
                        break;
                }
                std::string channelName;
                // Loop over the needed channels, and set each of them independantly
                for (unsigned int i = 0; i < 4 && !channels[i].empty(); ++i) {
                    channelName = std::string(paramName) + channels[i];
                    // always set the attribute value
                    attributes[i].Set(attrValues[i]);
                    // check if this channel is linked and connect the corresponding adapter attr.
                    // Note that we can call AiNodeGetLink with e.g. attr.r, attr.x, etc...
                    AtNode *channelTarget = AiNodeGetLink(node, channelName.c_str(), &outComp);
                    if (channelTarget) {
                        std::string channelTargetName = GetConnectedNode(writer, channelTarget, outComp);
                        if (!channelTargetName.empty())
                            attributes[i].AddConnection(SdfPath(channelTargetName));
                    } 
                }
            }
        }
    }
    return true;

}

/** 
 *    This function will export all the Arnold-specific attributes, as well as eventual User Data on
 *    this arnold node.
 **/
void UsdArnoldPrimWriter::writeArnoldParameters(
    const AtNode* node, UsdArnoldWriter& writer, UsdPrim& prim, const std::string& scope)
{
    // Loop over the arnold parameters, and write them
    const AtNodeEntry* nodeEntry = AiNodeGetNodeEntry(node);
    AtParamIterator* paramIter = AiNodeEntryGetParamIterator(nodeEntry);
    std::unordered_set<std::string> attrs;

    while (!AiParamIteratorFinished(paramIter)) {
        const AtParamEntry* paramEntry = AiParamIteratorGetNext(paramIter);
        const char* paramName(AiParamGetName(paramEntry));
        if (strcmp(paramName, "name") == 0) { // "name" is an exception and shouldn't be saved
            continue;
        }
        // This parameter was already exported, let's skip it
        if (!_exportedAttrs.empty() && std::find(_exportedAttrs.begin(), _exportedAttrs.end(), std::string(paramName)) != _exportedAttrs.end())
          continue;

        attrs.insert(paramName);
        UsdArnoldCustomParamWriter paramWriter(node, prim, paramEntry, scope);
        convertArnoldAttribute(node, prim, writer, *this, paramWriter);
    }
    AiParamIteratorDestroy(paramIter);

    // We also need to export all the user data set on this AtNode
    AtUserParamIterator* iter = AiNodeGetUserParamIterator(node);
    while (!AiUserParamIteratorFinished(iter)) {
        
        const AtUserParamEntry *paramEntry = AiUserParamIteratorGetNext(iter);
        const char *paramName = AiUserParamGetName (paramEntry);
        attrs.insert(paramName);
        UsdArnoldPrimvarWriter paramWriter(node, prim, paramEntry);
        convertArnoldAttribute(node, prim, writer, *this, paramWriter);
    }
    AiUserParamIteratorDestroy(iter);

    // Remember that all these attributes were exported to USD
    _exportedAttrs.insert(attrs.begin(), attrs.end());
}

//=================  Unsupported primitives
/**
 *   This function will be invoked for node types that are explicitely not
 *   supported. We'll dump a warning, saying that this node isn't supported in the
 *   USD conversion
 **/
void UsdArnoldWriteUnsupported::write(const AtNode* node, UsdArnoldWriter& writer)
{
    if (node == NULL) {
        return;
    }

    AiMsgWarning("UsdArnoldWriter : %s nodes not supported, cannot write %s", _type.c_str(), AiNodeGetName(node));
}

bool UsdArnoldPrimWriter::writeAttribute(const AtNode *node, const char *paramName, UsdPrim &prim, const UsdAttribute &attr, UsdArnoldWriter &writer)
{
    const AtParamEntry* paramEntry = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), AtString(paramName));
    if (!paramEntry)
        return false;

    UsdArnoldBuiltinParamWriter paramWriter(node, prim, paramEntry, attr);
    convertArnoldAttribute(node, prim, writer, *this, paramWriter);
    _exportedAttrs.insert(std::string(paramName)); // remember that we've explicitely exported this arnold attribute


    return true;
}

void UsdArnoldPrimWriter::writeMatrix(UsdGeomXformable &xformable, const AtNode *node, UsdArnoldWriter &writer)
{
    _exportedAttrs.insert("matrix");
    AtArray *array = AiNodeGetArray(node, "matrix");
    if (array == nullptr)
        return;

    unsigned int numKeys = AiArrayGetNumKeys(array);
    
    AtMatrix *matrices = (AtMatrix *)AiArrayMap(array);
    if (matrices == nullptr)
      return;

    bool hasMatrix = false;

    for (unsigned int i = 0; i < numKeys; ++i) {
        if (!AiM4IsIdentity(matrices[i])) {
            hasMatrix = true;
        }
    }
    // Identity matrix, nothing to write
    if (!hasMatrix) 
        return;

    UsdGeomXformOp xformOp = xformable.MakeMatrixXform();
    std::vector<double> xform;
    xform.reserve(16);
    // Get array of times based on motion_start / motion_end

    double m[4][4];
    float timeDelta = (numKeys > 1) ? (_motionEnd - _motionStart) / (int)(numKeys - 1) : 0.f;
    float time = _motionStart;

    for (unsigned int k = 0; k < numKeys; ++k) {
        AtMatrix &mtx = matrices[k];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                m[i][j] = mtx[i][j];
            }            
        }
        xformOp.Set(GfMatrix4d(m), UsdTimeCode(time));
        time += timeDelta;
    }
    AiArrayUnmap(array);
}

static void processMaterialBinding(AtNode *shader, AtNode *displacement, UsdPrim &prim, UsdArnoldWriter &writer)
{
    std::string shaderName = (shader) ? UsdArnoldPrimWriter::getArnoldNodeName(shader) : "";
    std::string dispName = (displacement) ? UsdArnoldPrimWriter::getArnoldNodeName(displacement) : "";

    // Special case : by default when no shader is assigned, the shader that is returned
    // is the arnold default shader "ai_default_reflection_shader". Since it's an implicit node that
    // isn't exported to arnold, we don't want to consider it
    if (shaderName == "/ai_default_reflection_shader") {
        shader = nullptr;
        shaderName = "";
    }
 
    if (shader == nullptr && displacement == nullptr)
        return; // nothing to export
   
    // The material node doesn't exist in Arnold, but is required in USD, 
    // let's create one based on the name of the shader plus the name of 
    // the eventual displacement. This way we'll have a unique material in USD
    // per combination of surface shader + displacement instead of duplicating it
    // for every geometry.
    std::string materialName = "/materials";
    materialName += shaderName;
    materialName += dispName;

    // Note that if the material was already created, Define will just return
    // the existing one
    UsdShadeMaterial mat = UsdShadeMaterial::Define(writer.getUsdStage(), SdfPath(materialName));
    
    // Bind the material to this primitive
    UsdShadeMaterialBindingAPI(prim).Bind(mat);

    // Now bind the eventual surface shader and displacement to the material.
    // Note that in theory, we shouldn't have to do all this if the material already existed,
    // so this could eventually be optimized
    TfToken arnoldContext("arnold");
    if (shader) {
        writer.writePrimitive(shader); // ensure the shader exists in the USD stage    
        UsdShadeOutput surfaceOutput = mat.CreateSurfaceOutput(arnoldContext);
        if (writer.getUsdStage()->GetPrimAtPath(SdfPath(shaderName))) {
            std::string surfaceTargetName = shaderName + std::string(".outputs:surface");
            surfaceOutput.ConnectToSource(SdfPath(surfaceTargetName));
        }
    }
    if (displacement) {
        writer.writePrimitive(displacement); // ensure the displacement shader exists in USD
        UsdShadeOutput dispOutput = mat.CreateDisplacementOutput(arnoldContext);
        if (writer.getUsdStage()->GetPrimAtPath(SdfPath(dispName))) {
            std::string dispTargetName = dispName + std::string(".outputs:displacement");
            dispOutput.ConnectToSource(SdfPath(dispTargetName));
        }
    }
}

void UsdArnoldPrimWriter::writeMaterialBinding(const AtNode *node, UsdPrim &prim, 
    UsdArnoldWriter &writer, AtArray *shidxsArray)
{
    _exportedAttrs.insert("shader");
    _exportedAttrs.insert("disp_map");

    // In polymeshes / curves, "shidxs" returns a shader index for each face / curve strand
    // This shader index is then used in the arrays "shader" and "disp_map" to determine
    // which shader to evaluate
    unsigned int shidxsCount = (shidxsArray) ? AiArrayGetNumElements(shidxsArray) : 0;

    if (shidxsCount > 0) {
        //--- Per-face shader assignments
        _exportedAttrs.insert("shidxs");

        UsdGeomImageable geom(prim);
        AtArray *shaders = AiNodeGetArray(node, "shader");
        static const AtString polymesh_str("polymesh");
        AtArray *displacements = (AiNodeIs(node, polymesh_str)) ?
            AiNodeGetArray(node, "disp_map"): nullptr;

        unsigned int numShaders = (shaders) ? AiArrayGetNumElements(shaders) : 0;
        unsigned int numDisp = (displacements) ? AiArrayGetNumElements(displacements) : 0;

        if (numShaders >= 1 || numDisp >= 1) {
            // Only create geom subsets if there is more than one shader / displacement.
            unsigned char* shidxs = (unsigned char*)AiArrayMap(shidxsArray);

            unsigned int numSubsets = AiMax(numShaders, numDisp);
            for (unsigned int i = 0; i < numSubsets; ++i) {
                AtNode *shader = (i < numShaders) ? (AtNode*)AiArrayGetPtr(shaders, i) : nullptr;
                AtNode *displacement = (i < numDisp) ? (AtNode*)AiArrayGetPtr(displacements, i) : nullptr;

                VtIntArray indices;
                // Append in this array all the indices that match the current shading subset
                for (int j = 0; j < shidxsCount; ++j) {
                    if (shidxs[j] == i)
                        indices.push_back(j);
                }

                UsdGeomSubset subset = UsdGeomSubset::CreateUniqueGeomSubset(
                                            geom, 
                                            TfToken("subset"),
                                            TfToken("face"), // currently the only supported type
                                            indices);
                UsdPrim subsetPrim = subset.GetPrim();

                // Process the material binding on the subset primitive
                processMaterialBinding(shader, displacement, subsetPrim, writer);
                
            }
            AiArrayUnmap(shidxsArray);
            return;
        }
    }

    //-- Single shader for the whole geometry
    AtNode *shader = (AtNode*) AiNodeGetPtr(node, "shader");
    static const AtString polymesh_str("polymesh");
    AtNode *displacement = (AiNodeIs(node, polymesh_str)) ?
        (AtNode*) AiNodeGetPtr(node, "disp_map"): nullptr;

    processMaterialBinding(shader, displacement, prim, writer);
}
