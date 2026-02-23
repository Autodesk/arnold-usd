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
#include "prim_writer.h"
#include <constant_strings.h>
#include <common_utils.h>

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

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (subset)
    (face)
    (materialBind)
    (partition)
    (displayColor)
    (displayOpacity)
    ((outputsOut, "outputs:out"))
    ((floatToRgba, "arnold:float_to_rgba"))
);

namespace {

inline GfMatrix4d _NodeGetGfMatrix(const AtNode* node, const char* param)
{
    const AtMatrix mat = AiNodeGetMatrix(node, AtString(param));
    GfMatrix4f matFlt(mat.data);
    return GfMatrix4d(matFlt);
};

inline const char* _GetEnum(AtEnum en, int32_t id)
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
          [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetByte(no, AtString(na))); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->BYTE() == AiNodeGetByte(no, AtString(na)));
          }}},
        {AI_TYPE_INT,
         {SdfValueTypeNames->Int,
          [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetInt(no, AtString(na))); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->INT() == AiNodeGetInt(no, AtString(na)));
          }}},
        {AI_TYPE_UINT,
         {SdfValueTypeNames->UInt,
          [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetUInt(no, AtString(na))); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->UINT() == AiNodeGetUInt(no, AtString(na)));
          }}},
        {AI_TYPE_BOOLEAN,
         {SdfValueTypeNames->Bool,
          [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetBool(no, AtString(na))); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->BOOL() == AiNodeGetBool(no, AtString(na)));
          }}},
        {AI_TYPE_FLOAT,
         {SdfValueTypeNames->Float,
          [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetFlt(no, AtString(na))); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->FLT() == AiNodeGetFlt(no, AtString(na)));
          }}},
        {AI_TYPE_RGB,
         {SdfValueTypeNames->Color3f,
          [](const AtNode* no, const char* na) -> VtValue {
              const auto v = AiNodeGetRGB(no, AtString(na));
              return VtValue(GfVec3f(v.r, v.g, v.b));
          },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->RGB() == AiNodeGetRGB(no, AtString(na)));
          }}},
        {AI_TYPE_RGBA,
         {SdfValueTypeNames->Color4f,
          [](const AtNode* no, const char* na) -> VtValue {
              const auto v = AiNodeGetRGBA(no, AtString(na));
              return VtValue(GfVec4f(v.r, v.g, v.b, v.a));
          },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->RGBA() == AiNodeGetRGBA(no, AtString(na)));
          }}},
        {AI_TYPE_VECTOR,
         {SdfValueTypeNames->Vector3f,
          [](const AtNode* no, const char* na) -> VtValue {
              const auto v = AiNodeGetVec(no, AtString(na));
              return VtValue(GfVec3f(v.x, v.y, v.z));
          },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->VEC() == AiNodeGetVec(no, AtString(na)));
          }}},
        {AI_TYPE_VECTOR2,
         {SdfValueTypeNames->Float2,
          [](const AtNode* no, const char* na) -> VtValue {
              const auto v = AiNodeGetVec2(no, AtString(na));
              return VtValue(GfVec2f(v.x, v.y));
          },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->VEC2() == AiNodeGetVec2(no, AtString(na)));
          }}},
        {AI_TYPE_STRING,
         {SdfValueTypeNames->String,
          [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetStr(no, AtString(na)).c_str()); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->STR() == AiNodeGetStr(no, AtString(na)));
          }}},
        {AI_TYPE_POINTER,
         {SdfValueTypeNames->String, nullptr, // TODO: how should we write pointer attributes ??
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (AiNodeGetPtr(no, AtString(na)) == nullptr);
          }}},
        {AI_TYPE_NODE,
         {SdfValueTypeNames->String,
          [](const AtNode* no, const char* na) -> VtValue {
              std::string targetName;
              AtNode* target = (AtNode*)AiNodeGetPtr(no, AtString(na));
              if (target) {
                  targetName = AiNodeGetName(target);
              }
              return VtValue(targetName);
          },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (AiNodeGetPtr(no, AtString(na)) == nullptr);
          }}},
        {AI_TYPE_MATRIX,
         {SdfValueTypeNames->Matrix4d,
          [](const AtNode* no, const char* na) -> VtValue { return VtValue(_NodeGetGfMatrix(no, na)); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return AiM4IsIdentity(AiNodeGetMatrix(no, AtString(na)));
          }}},
        {AI_TYPE_ENUM,
         {SdfValueTypeNames->Token,
          [](const AtNode* no, const char* na) -> VtValue {
              const auto* nentry = AiNodeGetNodeEntry(no);
              if (nentry == nullptr) {
                  return VtValue(TfToken());
              }
              const auto* pentry = AiNodeEntryLookUpParameter(nentry, AtString(na));
              if (pentry == nullptr) {
                  return VtValue(TfToken());
              }
              const auto enums = AiParamGetEnum(pentry);
              return VtValue(TfToken(_GetEnum(enums, AiNodeGetInt(no, AtString(na)))));
          },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->INT() == AiNodeGetInt(no, AtString(na)));
          }}},
        {AI_TYPE_CLOSURE,
         {SdfValueTypeNames->String, [](const AtNode* no, const char* na) -> VtValue { return VtValue(""); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool { return true; }}},
        {AI_TYPE_USHORT,
         {SdfValueTypeNames->UInt,
          [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetUInt(no, AtString(na))); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->UINT() == AiNodeGetUInt(no, AtString(na)));
          }}},
        {AI_TYPE_HALF,
         {SdfValueTypeNames->Half,
          [](const AtNode* no, const char* na) -> VtValue { return VtValue(AiNodeGetFlt(no, AtString(na))); },
          [](const AtNode* no, const char* na, const AtParamValue* pentry) -> bool {
              return (pentry->FLT() == AiNodeGetFlt(no, AtString(na)));
          }}}};
    return ret;
}

/**
 *  UsdArnoldBuiltinParamWriter handles the conversion of USD builtin attributes.
 *  These attributes already exist in the USD prim schemas so we just need to set
 *  their value
 **/
class UsdArnoldBuiltinParamWriter {
public:
    UsdArnoldBuiltinParamWriter(
        const AtNode* node, UsdPrim& prim, const AtParamEntry* paramEntry, const UsdAttribute& attr)
        : _paramEntry(paramEntry), _attr(attr)
    {
    }

    uint8_t GetParamType() const { return AiParamGetType(_paramEntry); }
    bool SkipDefaultValue(const UsdArnoldPrimWriter::ParamConversion* paramConversion) const { return false; }
    AtString GetParamName() const { return AiParamGetName(_paramEntry); }

    template <typename T>
    void ProcessAttribute(const UsdArnoldWriter &writer, UsdArnoldPrimWriter &primWriter, const SdfValueTypeName& typeName, const T& value)
    {
        // The UsdAttribute already exists, we just need to set it
        writer.SetAttribute(_attr, value);
    }
    template <typename T>
    void ProcessAttributeKeys(const UsdArnoldWriter &writer, UsdArnoldPrimWriter &primWriter, 
        const SdfValueTypeName& typeName, const std::vector<T>& values, float motionStart, float motionEnd)
    {
        if (values.empty())
            return;

        if (values.size() == 1) 
            writer.SetAttribute(_attr, values[0]);
        else {
            if (motionStart >= motionEnd) {
                // invalid motion start / end points, let's just write a single value
                writer.SetAttribute(_attr, values[0]);
            } else {
                float motionDelta = (motionEnd - motionStart) / ((int)values.size() - 1);
                float time = motionStart;
                for (size_t i = 0; i < values.size(); ++i, time += motionDelta) {
                    writer.SetAttribute(_attr, values[i], &time);
                }
            }
        }
    }
    void AddConnection(const SdfPath& path) { _attr.AddConnection(path); }
    const UsdAttribute& GetAttr() { return _attr; }

private:
    const AtParamEntry* _paramEntry;
    UsdAttribute _attr;
};

/**
 *  UsdArnoldCustomParamWriter handles the conversion of arnold-specific attributes,
 *  that do not exist in the USD native schemas. We need to create them with the right type,
 *  prefixing them with the "arnold:" namespace, and then we set their value.
 **/
class UsdArnoldCustomParamWriter {
public:
    UsdArnoldCustomParamWriter(
        const AtNode* node, UsdPrim& prim, const AtParamEntry* paramEntry, const std::string& scope)
        : _node(node), _prim(prim), _paramEntry(paramEntry), _scope(scope)
    {
    }
    uint8_t GetParamType() const { return AiParamGetType(_paramEntry); }
    bool SkipDefaultValue(const UsdArnoldPrimWriter::ParamConversion* paramConversion) const
    {
        AtString paramNameStr = GetParamName();
        return paramConversion && paramConversion->d &&
               paramConversion->d(_node, paramNameStr.c_str(), AiParamGetDefault(_paramEntry));
    }
    AtString GetParamName() const { return AiParamGetName(_paramEntry); }

    template <typename T>
    void ProcessAttribute(const UsdArnoldWriter &writer, UsdArnoldPrimWriter &primWriter, const SdfValueTypeName& typeName, T& value)
    {
        // Create the UsdAttribute, in the desired scope, and set its value
        AtString paramNameStr = GetParamName();
        std::string paramName(paramNameStr.c_str());
        std::string usdParamName = (_scope.empty()) ? paramName : _scope + std::string(":") + paramName;

        int paramType = GetParamType();
        // Some arnold string attributes can actually represent USD asset attributes.
        // In order to identify them, we check for a specific metadata "path" set on this attribute
        if (paramType == AI_TYPE_STRING) {
            const AtNodeEntry *nentry = AiNodeGetNodeEntry(_node);
            AtString pathMetadata;
            if (AiMetaDataGetStr(nentry, paramNameStr, str::path, &pathMetadata) && 
                    pathMetadata == str::file) {

                VtValue* vtVal = (VtValue*)(&value);
                SdfAssetPath assetPath(vtVal->Get<std::string>());
                _attr = _prim.CreateAttribute(TfToken(usdParamName), SdfValueTypeNames->Asset, false);
                writer.SetAttribute(_attr, VtValue(assetPath));
                return;
            }
        } else if (paramType == AI_TYPE_NODE)
        {
            AtNode *target = (AtNode*) AiNodeGetPtr(_node, paramNameStr);
            if (target) { 
                if (AiNodeEntryGetType(AiNodeGetNodeEntry(target)) & UsdArnoldPrimWriter::GetShadersMask()) {
                    // If this attribute is pointing to a "shader" primitive
                    // (which also includes operators & imagers) then we must notify that this 
                    // primitive is required. We'll use an ArnoldNodeGraph primitive to ensure
                    // it's loaded in hydra
                    const_cast<UsdArnoldWriter*>(&writer)->RequiresShader(target);
                }
            }
        }
        _attr = _prim.CreateAttribute(TfToken(usdParamName), typeName, false);
        writer.SetAttribute(_attr, value);
    }
    template <typename T>
    void ProcessAttributeKeys(const UsdArnoldWriter &writer, UsdArnoldPrimWriter &primWriter, 
        const SdfValueTypeName& typeName, const std::vector<T>& values, float motionStart, float motionEnd)
    {
        if (values.empty())
            return;

        if (values.size() == 1) {
            ProcessAttribute(writer, primWriter, typeName, values[0]);
            return;
        }
        // Create the UsdAttribute, in the desired scope, and set its value
        AtString paramNameStr = GetParamName();
        std::string paramName(paramNameStr.c_str());
        std::string usdParamName = (_scope.empty()) ? paramName : _scope + std::string(":") + paramName;
        _attr = _prim.CreateAttribute(TfToken(usdParamName), typeName, false);

        if (motionStart >= motionEnd) {
            // invalid motion start / end points, let's just write a single value
            writer.SetAttribute(_attr, values[0]);
        } else {
            float motionDelta = (motionEnd - motionStart) / ((int)values.size() - 1);
            float time = motionStart;
            for (size_t i = 0; i < values.size(); ++i, time += motionDelta) {
                writer.SetAttribute(_attr, values[i], &time);
            }
        }
    }
    void AddConnection(const SdfPath& path) { _attr.AddConnection(path); }
    const UsdAttribute& GetAttr() { return _attr; }

private:
    const AtNode* _node;
    UsdPrim& _prim;
    const AtParamEntry* _paramEntry;
    std::string _scope;
    UsdAttribute _attr;
};

/**
 *  UsdArnoldPrimvarWriter handles the conversion of arnold user data, that must
 *  be exported as USD primvars (without any namespace).
 **/
class UsdArnoldPrimvarWriter {
public:
    UsdArnoldPrimvarWriter(
        const AtNode* node, UsdPrim& prim, const AtUserParamEntry* userParamEntry, UsdArnoldWriter& writer)
        : _node(node), _userParamEntry(userParamEntry), _writer(writer), _primvarsAPI(prim)
    {
    }

    bool SkipDefaultValue(const UsdArnoldPrimWriter::ParamConversion* paramConversion) const { return false; }
    uint8_t GetParamType() const
    {
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

    AtString GetParamName() const { return AtString(AiUserParamGetName(_userParamEntry)); }

    template <typename T>
    void ProcessAttribute(const UsdArnoldWriter &writer, UsdArnoldPrimWriter &primWriter, const SdfValueTypeName& typeName, T& value)
    {
        SdfValueTypeName type = typeName;

        uint8_t paramType = GetParamType();
        TfToken category;
        AtString paramNameStr = GetParamName();
        const char* paramName = paramNameStr.c_str();
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
#ifdef USE_NATIVE_INSTANCING
            case AI_USERDEF_PER_INSTANCE:
                category = str::t_instance;
                break;
#endif
            case AI_USERDEF_CONSTANT:
            default:
                category = UsdGeomTokens->constant;
        }
        
        // Special case for displayColor, that needs to be set as a color array
        static AtString displayColorStr("displayColor");
        if (paramNameStr == displayColorStr && type == SdfValueTypeNames->Color3f) {
            if (std::is_same<T, VtValue>::value) {
                VtValue* vtVal = (VtValue*)(&value);
                VtArray<GfVec3f> arrayValue;
                arrayValue.push_back(vtVal->Get<GfVec3f>());
                UsdGeomPrimvar primVar = _primvarsAPI.GetPrimvar(_tokens->displayColor);
                if (primVar)
                    writer.SetPrimVar(primVar, arrayValue);
            }
            return;
        }
        // Same for displayOpacity, as a float array
        static AtString displayOpacityStr("displayOpacity");
        if (paramNameStr == displayOpacityStr && type == SdfValueTypeNames->Float) {
            if (std::is_same<T, VtValue>::value) {
                VtValue* vtVal = (VtValue*)(&value);
                VtArray<float> arrayValue;
                arrayValue.push_back(vtVal->Get<float>());
                UsdGeomPrimvar primVar = _primvarsAPI.GetPrimvar(_tokens->displayOpacity);
                if (primVar)
                    writer.SetPrimVar(primVar, arrayValue);
            }
            return;
        }

        _primVar = _primvarsAPI.CreatePrimvar(TfToken(paramName), type, category);
        writer.SetPrimVar(_primVar, value);

        if (category == UsdGeomTokens->faceVarying) {
            // in case of indexed user data, we need to find the arnold array with an "idxs" suffix
            // (arnold convention), and set it as the primVar indices
            std::string indexAttr = std::string(paramNameStr.c_str()) + std::string("idxs");
            AtString indexAttrStr(indexAttr.c_str());
            AtArray* indexArray = AiNodeGetArray(_node, indexAttrStr);
            unsigned int indexArraySize = (indexArray) ? AiArrayGetNumElements(indexArray) : 0;
            if (indexArraySize > 0) {
                VtIntArray vtIndices(indexArraySize);
                for (unsigned int i = 0; i < indexArraySize; ++i) {
                    vtIndices[i] = AiArrayGetInt(indexArray, i);
                }
                writer.SetPrimVarIndices(_primVar, vtIndices);
                primWriter.AddExportedAttr(indexAttr);
            }
        }

        if (paramType == AI_TYPE_NODE) {
            AtNode* target = (AtNode*)AiNodeGetPtr(_node, paramNameStr);
            if (target) {
                _writer.WritePrimitive(target); // ensure the target is written first
                std::string targetName = UsdArnoldPrimWriter::GetArnoldNodeName(target, writer);
                _primVar.GetAttr().AddConnection(SdfPath(targetName));
            }
        }
    }
    template <typename T>
    void ProcessAttributeKeys(const UsdArnoldWriter &writer, UsdArnoldPrimWriter &primWriter, 
        const SdfValueTypeName& typeName, const std::vector<T>& values, float motionStart, float motionEnd)
    {
        if (!values.empty())
            ProcessAttribute(writer, primWriter, typeName, values[0]);
        // we're currently not supporting motion blur in primvars
    }

    void AddConnection(const SdfPath& path)
    {
        if (_primVar) {
            _primVar.GetAttr().AddConnection(path);
        }
    }
    const UsdAttribute& GetAttr() { return _primVar.GetAttr(); }

private:
    const AtNode* _node;
    const AtUserParamEntry* _userParamEntry;
    UsdArnoldWriter& _writer;
    UsdGeomPrimvarsAPI _primvarsAPI;
    UsdGeomPrimvar _primVar;
};

} // namespace

// Get the conversion item for this node type (see above)
const UsdArnoldPrimWriter::ParamConversion* UsdArnoldPrimWriter::GetParamConversion(uint8_t type)
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
void UsdArnoldPrimWriter::WriteNode(const AtNode* node, UsdArnoldWriter& writer)
{
    // we're exporting a new node, so we store the previous list of exported
    // attributes and we clear it for the new node being written
    decltype(_exportedAttrs) prevExportedAttrs;
    prevExportedAttrs.swap(_exportedAttrs);

    const AtNodeEntry *entry = AiNodeGetNodeEntry(node);

    _motionStart = (AiNodeEntryLookUpParameter(entry, AtString("motion_start")))
                       ? AiNodeGetFlt(node, AtString("motion_start"))
                       : writer.GetShutterStart();
    _motionEnd = (AiNodeEntryLookUpParameter(entry, AtString("motion_end"))) ? AiNodeGetFlt(node, AtString("motion_end"))
                                                                                      : writer.GetShutterEnd();


    // Now call the virtual function write() defined for each primitive type
    Write(node, writer);

    // restore the previous list (likely empty, unless there have been recursive creation of nodes)
    _exportedAttrs.swap(prevExportedAttrs);

    // Remember all shader AtNodes that were exported. We don't want to re-export them
    // in the last shader loop
    if (AiNodeEntryGetType(entry) & GetShadersMask())
        writer.SetExportedShader(node);
}
/**
 *    Get the USD node name for this Arnold node. We need to replace the
 *forbidden characters from the names. Also, we must ensure that the first
 *character is a slash
 **/
std::string UsdArnoldPrimWriter::GetArnoldNodeName(const AtNode* node, const UsdArnoldWriter &writer)
{
    std::string name = AiNodeGetName(node);
    // The global options node should always be named the same way in USD, /Render/settings
    if (AiNodeIs(node, str::options)) {
        return writer.GetRenderScope().GetString() + std::string("/settings");
    }

    if (name.empty()) {
        // Arnold can have nodes with empty names, but this is forbidden in USD.
        // We're going to generate an arbitrary name for this node, with its node type
        // and a naùe based on its pointer #380
        std::stringstream ss;
        ss << "unnamed/" << AiNodeEntryGetName(AiNodeGetNodeEntry(node)) << "/p" << node;
        name = ss.str();
    }

    _SanitizePrimName(name);
    
    // If we need to strip a hierarchy from the arnold node's name,
    // we need to find if this node name starts with the expected hierarchy
    // and do it before prefixing it with the scope
    const std::string &stripHierarchy = writer.GetStripHierarchy();
    if (!stripHierarchy.empty()) {
        if (TfStringStartsWith(name, stripHierarchy)) {
            name = name.substr(stripHierarchy.size());
        }
    }
    name = writer.GetScope() + name;

    const AtNodeEntry* nodeEntry = AiNodeGetNodeEntry(node);
    // Drivers should always be under the scope /Render/Products
    if (AiNodeEntryGetType(nodeEntry) == AI_NODE_DRIVER) {
        name = writer.GetRenderProductsScope().GetString() + name;
    } 
    
    return name;
}
void UsdArnoldPrimWriter::_SanitizePrimName(std::string &name)
{
    std::locale loc;    

    // We need to determine which parameters must be converted to underscores
    // and which must be converted to slashes. In Maya node names, pipes
    // correspond to hierarchies so for now I'm converting them to slashes.
    for (size_t i = 0; i < name.length(); ++i) {
        char &c = name[i];

        if (c == '|')
            c = '/';
        else if (c == '@' || c == '.' || c == ':' || c == '-' || c == '*')
            c = '_';

        if (i == 0 && c != '/')
            name.insert(0, 1, '/'); // (this invalidates the "c" variable)

        // If the first character after each '/' is a digit, USD will complain.
        // We'll insert a dummy character in that case    
        if (name[i] == '/' && i < (name.length() - 1) && std::isdigit(name[i+1], loc)) {
            name.insert(i+1, 1, '_');
            i++;
        }
    }
}
// Ensure a connected node is properly translated, handle the output attributes,
// and return its name
static inline std::string GetConnectedNode(UsdArnoldWriter& writer, AtNode* target, int outComp = -1)
{
    // First, ensure the primitive was written to usd
    writer.WritePrimitive(target);

    // Get the usd name of this prim
    std::string targetName = UsdArnoldPrimWriter::GetArnoldNodeName(target, writer);
    UsdPrim targetPrim = writer.GetUsdStage()->GetPrimAtPath(SdfPath(targetName));

    // ensure the prim exists for the link
    if (!targetPrim)
        return std::string();

    // check the output type of this node
    int targetEntryType = AiNodeEntryGetOutputType(AiNodeGetNodeEntry(target));
    if (outComp < 0) { // Connection on the full node output
        SdfValueTypeName type;
        const auto outputIterType = UsdArnoldPrimWriter::GetParamConversion(targetEntryType);
        if (outputIterType) {
            // Create the output attribute on the node, of the corresponding type
            // For now we call it outputs:out to be generic, but it could be called rgb, vec, float, etc...
            UsdAttribute attr = targetPrim.CreateAttribute(_tokens->outputsOut, outputIterType->type, false);
            // the connection will point at this output attribute
            targetName += ".outputs:out";
        }
    } else { // connection on an output component (r, g, b, etc...)
        std::string compList;
        // we support components on vectors and colors only, and they're
        // always represented by a single character.
        // This string contains the sequence for each of these characters
        switch (targetEntryType) {
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
static inline bool convertArnoldAttribute(
    const AtNode* node, UsdPrim& prim, UsdArnoldWriter& writer, UsdArnoldPrimWriter& primWriter, T& attrWriter)
{
    int paramType = attrWriter.GetParamType();
    const char* paramName = attrWriter.GetParamName();

    if (paramType == AI_TYPE_ARRAY) {
        AtArray* array = AiNodeGetArray(node, AtString(paramName));
        if (array == nullptr) {
            return false;
        }
        int arrayType = AiArrayGetType(array);
        unsigned int numElements = AiArrayGetNumElements(array);
        if (numElements == 0 && !writer.GetWriteAllAttributes()) {
            return false;
        }
        unsigned int numKeys = AiArrayGetNumKeys(array);
        float motionStart = primWriter.GetMotionStart();
        float motionEnd = primWriter.GetMotionEnd();

        // Special case for shaders, animated arrays won't be supported in hydra, 
        // since the hydra material framework only provides us with a single value
        // for each attribute (as opposed to sampled values for different keys).
        // So when we write an animated shader attribute to usd, we want to write
        // them as a set of values for a single time. So if our arnold array has
        // one element and 3 keys, we want to author it as 3 values for a single time.
        // At the moment this use case only happens for shader matrix_interpolate,
        // which doesn't make any distinction between keys and elements (see #2080) 
        if (numKeys > 1) {
            if (AiNodeEntryGetType(AiNodeGetNodeEntry(node)) == AI_NODE_SHADER) {
                numElements *= numKeys;
                numKeys = 1;
            }
        }

        SdfValueTypeName typeName;
        switch (arrayType) {
            case AI_TYPE_BYTE: {
                std::vector<VtArray<unsigned char> > vtMotionArray(numKeys);
                const unsigned char* arrayMap = (const unsigned char*)AiArrayMapConst(array);
                for (unsigned int j = 0; j < numKeys; ++j) {
                    VtArray<unsigned char>& vtArr = vtMotionArray[j];
                    vtArr.resize(numElements);
                    memcpy(&vtArr[0], &arrayMap[j * numElements], numElements * sizeof(unsigned char));
                }
                typeName = SdfValueTypeNames->UCharArray;
                attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                AiArrayUnmap(array);
                break;
            }
            case AI_TYPE_INT: {
                std::vector<VtArray<int> > vtMotionArray(numKeys);
                const int* arrayMap = (const int*)AiArrayMapConst(array);
                for (unsigned int j = 0; j < numKeys; ++j) {
                    VtArray<int>& vtArr = vtMotionArray[j];
                    vtArr.resize(numElements);
                    memcpy(&vtArr[0], &arrayMap[j * numElements], numElements * sizeof(int));
                }
                typeName = SdfValueTypeNames->IntArray;
                attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                AiArrayUnmapConst(array);
                break;
            }
            case AI_TYPE_UINT: {
                std::vector<VtArray<unsigned int> > vtMotionArray(numKeys);
                const unsigned int* arrayMap = (const unsigned int*)AiArrayMapConst(array);
                for (unsigned int j = 0; j < numKeys; ++j) {
                    VtArray<unsigned int>& vtArr = vtMotionArray[j];
                    vtArr.resize(numElements);
                    memcpy(&vtArr[0], &arrayMap[j * numElements], numElements * sizeof(unsigned int));
                }
                typeName = SdfValueTypeNames->UIntArray;
                attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                AiArrayUnmapConst(array);
                break;
            }
            case AI_TYPE_BOOLEAN: {
                std::vector<VtArray<bool> > vtMotionArray(numKeys);
                const bool* arrayMap = (const bool*)AiArrayMapConst(array);
                for (unsigned int j = 0; j < numKeys; ++j) {
                    VtArray<bool>& vtArr = vtMotionArray[j];
                    vtArr.resize(numElements);
                    memcpy(&vtArr[0], &arrayMap[j * numElements], numElements * sizeof(bool));
                }
                typeName = SdfValueTypeNames->BoolArray;
                attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                AiArrayUnmap(array);
                break;
            }
            case AI_TYPE_FLOAT: {
                std::vector<VtArray<float> > vtMotionArray(numKeys);
                const float* arrayMap = (const float*)AiArrayMapConst(array);
                for (unsigned int j = 0; j < numKeys; ++j) {
                    VtArray<float>& vtArr = vtMotionArray[j];
                    vtArr.resize(numElements);
                    memcpy(&vtArr[0], &arrayMap[j * numElements], numElements * sizeof(float));
                }
                typeName = SdfValueTypeNames->FloatArray;
                attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                AiArrayUnmapConst(array);
                break;
            }
            case AI_TYPE_RGB: {
                std::vector<VtArray<GfVec3f> > vtMotionArray(numKeys);
                const GfVec3f* arrayMap = (GfVec3f*)AiArrayMapConst(array);
                for (unsigned int j = 0; j < numKeys; ++j) {
                    VtArray<GfVec3f>& vtArr = vtMotionArray[j];
                    vtArr.resize(numElements);
                    memcpy(&vtArr[0], &arrayMap[j * numElements], numElements * sizeof(GfVec3f));
                }
                typeName = SdfValueTypeNames->Color3fArray;
                attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                AiArrayUnmap(array);
                break;
            }
            case AI_TYPE_VECTOR: {
                std::vector<VtArray<GfVec3f> > vtMotionArray(numKeys);
                const GfVec3f* arrayMap = (GfVec3f*)AiArrayMapConst(array);
                for (unsigned int j = 0; j < numKeys; ++j) {
                    VtArray<GfVec3f>& vtArr = vtMotionArray[j];
                    vtArr.resize(numElements);
                    memcpy(&vtArr[0], &arrayMap[j * numElements], numElements * sizeof(GfVec3f));
                }
                typeName = SdfValueTypeNames->Vector3fArray;
                attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                AiArrayUnmap(array);
                break;
            }
            case AI_TYPE_RGBA: {
                std::vector<VtArray<GfVec4f> > vtMotionArray(numKeys);
                const GfVec4f* arrayMap = (GfVec4f*)AiArrayMapConst(array);
                for (unsigned int j = 0; j < numKeys; ++j) {
                    VtArray<GfVec4f>& vtArr = vtMotionArray[j];
                    vtArr.resize(numElements);
                    memcpy(&vtArr[0], &arrayMap[j * numElements], numElements * sizeof(GfVec4f));
                }
                typeName = SdfValueTypeNames->Color4fArray;
                attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                AiArrayUnmap(array);
                break;
            }
            case AI_TYPE_VECTOR2: {
                std::vector<VtArray<GfVec2f> > vtMotionArray(numKeys);
                const GfVec2f* arrayMap = (GfVec2f*)AiArrayMapConst(array);
                for (unsigned int j = 0; j < numKeys; ++j) {
                    VtArray<GfVec2f>& vtArr = vtMotionArray[j];
                    vtArr.resize(numElements);
                    memcpy(&vtArr[0], &arrayMap[j * numElements], numElements * sizeof(GfVec2f));
                }
                typeName = SdfValueTypeNames->Float2Array;
                attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                AiArrayUnmap(array);
                break;
            }
            case AI_TYPE_STRING: {
                // No animation for string arrays
                VtArray<std::string> vtArr(numElements);
                for (unsigned int i = 0; i < numElements; ++i) {
                    AtString str = AiArrayGetStr(array, i);
                    vtArr[i] = str.c_str();
                }
                typeName = SdfValueTypeNames->StringArray;
                attrWriter.ProcessAttribute(writer, primWriter, typeName, vtArr);
                break;
            }
            case AI_TYPE_MATRIX: {
                std::vector<VtArray<GfMatrix4d> > vtMotionArray(numKeys);
                const AtMatrix* arrayMap = (AtMatrix*)AiArrayMapConst(array);
                if (arrayMap) {
                    int index = 0;
                    for (unsigned int j = 0; j < numKeys; ++j) {
                        VtArray<GfMatrix4d>& vtArr = vtMotionArray[j];
                        vtArr.resize(numElements);
                        for (unsigned int i = 0; i < numElements; ++i, ++index) {
                            const AtMatrix mat = arrayMap[index];
                            GfMatrix4f matFlt(mat.data);
                            vtArr[i] = GfMatrix4d(matFlt);
                        }
                    }
                    typeName = SdfValueTypeNames->Matrix4dArray;
                    attrWriter.ProcessAttributeKeys(writer, primWriter, typeName, vtMotionArray, motionStart, motionEnd);
                }
                AiArrayUnmap(array);
                break;
            }

            case AI_TYPE_NODE: {
                // only export the first element for now
                VtArray<std::string> vtArr(numElements);
                for (unsigned int i = 0; i < numElements; ++i) {
                    AtNode* target = (AtNode*)AiArrayGetPtr(array, i);
                    vtArr[i] = (target) ? AiNodeGetName(target) : "";

                    // If this node attribute is pointing to a shader, we want to
                    // notify the writer that this shader is required. This way it will be put
                    // under an ArnoldNodeGraph primitive, which will allow it to show up in hydra.
                    if (target != nullptr && AiNodeEntryGetType(AiNodeGetNodeEntry(target)) == AI_NODE_SHADER)
                        writer.RequiresShader(target);
                }

                if (strcmp(paramName, "shader") == 0 && numElements == 1) {
                    if (vtArr[0] == "ai_default_reflection_shader") {
                        break;
                    }
                }
                typeName = SdfValueTypeNames->StringArray;
                attrWriter.ProcessAttribute(writer, primWriter, typeName, vtArr);
                break;
            }
            default:
                break;
        }
        if (AiNodeIsLinked(node, AtString(paramName)) && typeName) {
            // Linked array attributes : this means that some of the array elements are
            // linked to other shaders. This isn't supported natively in USD, so we need
            // to write it in a specific format. If attribute "attr" has element 1 linked to
            // a shader, we will write it as attr:i1
            std::string indexStr;
            std::string paramElemName;
            for (unsigned int i = 0; i < numElements; ++i) {
                indexStr = std::to_string(i);
                paramElemName = paramName + std::string("[") + indexStr + std::string("]");
                int outComp = -1;
                AtNode* arrayLink = AiNodeGetLink(node, paramElemName.c_str(), &outComp);
                if (arrayLink == nullptr)
                    continue;
                std::string targetName = GetConnectedNode(writer, arrayLink, outComp);

                paramElemName = attrWriter.GetAttr().GetName().GetText();
                paramElemName += std::string(":i") + indexStr;
                UsdAttribute elemAttr = prim.CreateAttribute(TfToken(paramElemName), typeName.GetScalarType(), false);
                elemAttr.AddConnection(SdfPath(targetName));
            }
        }
    } else {
        const auto iterType = UsdArnoldPrimWriter::GetParamConversion(paramType);
        bool isLinked = AiNodeIsLinked(node, AtString(paramName));
        if (!isLinked && !writer.GetWriteAllAttributes() && attrWriter.SkipDefaultValue(iterType)) {
            return false;
        }
        if (iterType != nullptr && iterType->f != nullptr) {
            VtValue value = iterType->f(node, AtString(paramName));
            attrWriter.ProcessAttribute(writer, primWriter, iterType->type, value);
        }

        if (isLinked) {
            int outComp = -1;
            AtNode* target = AiNodeGetLink(node, AtString(paramName), &outComp);
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
                UsdShadeShader shaderAPI = UsdShadeShader::Define(writer.GetUsdStage(), SdfPath(adapterName));
                // float_to_rgba can be used to convert rgb, rgba, vector, and vector2
                writer.SetAttribute(shaderAPI.CreateIdAttr(), _tokens->floatToRgba);

                UsdPrim shaderPrim = shaderAPI.GetPrim();
                UsdAttribute outAttr = shaderPrim.CreateAttribute(_tokens->outputsOut, SdfValueTypeNames->Color4f, false);
                // the connection will point at this output attribute
                std::string outAttrName = adapterName + std::string(".outputs:out");
                // connect the attribute to the adapter
                attrWriter.AddConnection(SdfPath(outAttrName));

                UsdAttribute attributes[4];
                float defaultValues[4] = {0.f, 0.f, 0.f, 1.f};
                std::string attrNames[4] = {"inputs:r", "inputs:g", "inputs:b", "inputs:a"};
                for (unsigned int i = 0; i < 4; ++i) {
                    attributes[i] =
                        shaderPrim.CreateAttribute(TfToken(attrNames[i]), SdfValueTypeNames->Float, false);
                    writer.SetAttribute(attributes[i], defaultValues[i]);                    
                }
                float attrValues[4] = {0.f, 0.f, 0.f, 0.f};
                std::vector<std::string> channels(4);
                switch (paramType) {
                    case AI_TYPE_VECTOR: {
                        channels[0] = ".x";
                        channels[1] = ".y";
                        channels[2] = ".z";
                        AtVector vec = AiNodeGetVec(node, AtString(paramName));
                        attrValues[0] = vec.x;
                        attrValues[1] = vec.y;
                        attrValues[2] = vec.z;
                        break;
                    }
                    case AI_TYPE_VECTOR2: {
                        channels[0] = ".x";
                        channels[1] = ".y";
                        AtVector2 vec = AiNodeGetVec2(node, AtString(paramName));
                        attrValues[0] = vec.x;
                        attrValues[1] = vec.y;
                        break;
                    }
                    case AI_TYPE_RGBA: {
                        channels[0] = ".r";
                        channels[1] = ".g";
                        channels[2] = ".b";
                        channels[3] = ".a";
                        AtRGBA col = AiNodeGetRGBA(node, AtString(paramName));
                        attrValues[0] = col.r;
                        attrValues[1] = col.g;
                        attrValues[2] = col.b;
                        attrValues[3] = col.a;
                        break;
                    }
                    case AI_TYPE_RGB: {
                        channels[0] = ".r";
                        channels[1] = ".g";
                        channels[2] = ".b";
                        AtRGB col = AiNodeGetRGB(node, AtString(paramName));
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
                    writer.SetAttribute(attributes[i], attrValues[i]);
                    
                    // check if this channel is linked and connect the corresponding adapter attr.
                    // Note that we can call AiNodeGetLink with e.g. attr.r, attr.x, etc...
                    AtNode* channelTarget = AiNodeGetLink(node, channelName.c_str(), &outComp);
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
void UsdArnoldPrimWriter::_WriteArnoldParameters(
    const AtNode* node, UsdArnoldWriter& writer, UsdPrim& prim, const std::string& scope)
{
    // Loop over the arnold parameters, and write them
    const AtNodeEntry* nodeEntry = AiNodeGetNodeEntry(node);
    AtParamIterator* paramIter = AiNodeEntryGetParamIterator(nodeEntry);
    std::unordered_set<std::string> attrs;

    while (!AiParamIteratorFinished(paramIter)) {
        const AtParamEntry* paramEntry = AiParamIteratorGetNext(paramIter);
        const char* paramName(AiParamGetName(paramEntry));

        // This parameter was already exported, let's skip it
        if (!_exportedAttrs.empty() &&
            std::find(_exportedAttrs.begin(), _exportedAttrs.end(), std::string(paramName)) != _exportedAttrs.end())
            continue;

        // We only save attribute "name" if it's different from the primitive name, 
        // and if there is no scope.
        if (strcmp(paramName, "name") == 0) {
            std::string arnoldNodeName = AiNodeGetName(node);
            std::string usdPrimName = prim.GetPath().GetText();

            // When we author shader primitives we must ensure the name 
            // will be stored as it can be renamed inside its material later on.
            // Let's force this by clearing the name used to do the comparison
            if (AiNodeEntryGetType(nodeEntry) & GetShadersMask())
                usdPrimName.clear();

            if (arnoldNodeName == usdPrimName || !writer.GetScope().empty())
                continue;            
        }
        
        attrs.insert(paramName);
        UsdArnoldCustomParamWriter paramWriter(node, prim, paramEntry, scope);
        convertArnoldAttribute(node, prim, writer, *this, paramWriter);
    }
    AiParamIteratorDestroy(paramIter);

    // We also need to export all the user data set on this AtNode
    AtUserParamIterator* iter = AiNodeGetUserParamIterator(node);
    while (!AiUserParamIteratorFinished(iter)) {
        const AtUserParamEntry* paramEntry = AiUserParamIteratorGetNext(iter);
        const char* paramName = AiUserParamGetName(paramEntry);
        if (!_exportedAttrs.empty() &&
            std::find(_exportedAttrs.begin(), _exportedAttrs.end(), std::string(paramName)) != _exportedAttrs.end())
            continue;
        attrs.insert(paramName);
        UsdArnoldPrimvarWriter paramWriter(node, prim, paramEntry, writer);
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
void UsdArnoldWriteUnsupported::Write(const AtNode* node, UsdArnoldWriter& writer)
{
    if (node == nullptr) {
        return;
    }

    AiMsgWarning("UsdArnoldWriter : %s nodes not supported, cannot write %s", _type.c_str(), AiNodeGetName(node));
}

bool UsdArnoldPrimWriter::WriteAttribute(
    const AtNode* node, const char* paramName, UsdPrim& prim, const UsdAttribute& attr, UsdArnoldWriter& writer)
{
    const AtParamEntry* paramEntry = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), AtString(paramName));
    if (!paramEntry)
        return false;

    UsdArnoldBuiltinParamWriter paramWriter(node, prim, paramEntry, attr);
    convertArnoldAttribute(node, prim, writer, *this, paramWriter);
    _exportedAttrs.insert(std::string(paramName)); // remember that we've explicitely exported this arnold attribute

    return true;
}

void UsdArnoldPrimWriter::_WriteMatrix(UsdGeomXformable& xformable, const AtNode* node, UsdArnoldWriter& writer)
{
    _exportedAttrs.insert("matrix");

    const UsdPrim &prim = xformable.GetPrim();
    const AtUniverse* universe = writer.GetUniverse();

    AtMatrix invParentMtx = AiM4Identity();
    bool applyInvParentMtx = false;
    // Iterator through USD parents until we find one matching an arnold node.
    // This node's matrix will give us the parent transform at this level of the USD hierarchy.
    // We'll need to apply the inverse of this transform to our node's matrix so that its final
    // world transform matches the arnold scene #2415
    for (UsdPrim p = prim.GetParent(); !p.IsPseudoRoot(); p = p.GetParent()) {
        
        // By default the arnold node name is the same as the usd prim path, 
        // unless we authored primvars:arnold:name on the primitive
        std::string parentName = p.GetPath().GetString();
        UsdAttribute parentNameAttr = p.GetAttribute(str::t_primvars_arnold_name);
        if (parentNameAttr && parentNameAttr.HasAuthoredValue()) {
            VtValue parentNameValue;
            if (parentNameAttr.Get(&parentNameValue) && parentNameValue.IsHolding<std::string>())
                parentName = parentNameValue.UncheckedGet<std::string>();
        }
        
        AtNode *parent = AiNodeLookUpByName(universe, AtString(parentName.c_str()));
        if (parent == nullptr)
            continue;
        // Special case for mesh lights pointing at our current mesh. Their transform will not be authored
        // to USD so we must skip it here.
        if (AiNodeIs(parent, str::mesh_light) && AiNodeGetPtr(parent, str::mesh) == (void*)node)
            continue;
        
        AtMatrix parentMatrix = AiNodeGetMatrix(parent, str::matrix);
        if (!AiM4IsIdentity(parentMatrix)) {
            invParentMtx = AiM4Invert(parentMatrix);
            applyInvParentMtx = true;
        }
        break;
    }
    
    AtArray* array = AiNodeGetArray(node, AtString("matrix"));
    unsigned int numKeys = array ? AiArrayGetNumKeys(array) : 1;
    const AtMatrix* matrices = array ? (const AtMatrix*)AiArrayMapConst(array) : nullptr;
    if (matrices == nullptr && !applyInvParentMtx)
        return;

    bool hasMatrix = applyInvParentMtx;

    if (matrices && !hasMatrix) {
        for (unsigned int i = 0; i < numKeys; ++i) {
            if (!AiM4IsIdentity(matrices[i])) {
                hasMatrix = true;
            }
        }
    }
    // Identity matrix, nothing to write
    if (!hasMatrix)
        return;

    UsdGeomXformOp xformOp = xformable.MakeMatrixXform();
    UsdAttribute attr = xformOp.GetAttr();

    if (!writer.GetAuthoredFrames().empty()) {
        VtValue previousVal;
        // If previous frames were authored, we want to verify
        // that a value was already set. If not, it means that
        // the previous value was an identify matrix, and thus
        // skipped a few lines above. We need to set it now
        // before we call SetAttribute (see #871)
        if (!attr.Get(&previousVal)) {
            GfMatrix4d m;
            attr.Set(m);
        }
    }

    std::vector<double> xform;
    xform.reserve(16);
    // Get array of times based on motion_start / motion_end
    
    double m[4][4];
    bool hasMotion = (numKeys > 1);
    float timeDelta =  hasMotion ? (_motionEnd - _motionStart) / (int)(numKeys - 1) : 0.f;
    float time = _motionStart;

    for (unsigned int k = 0; k < numKeys; ++k) {
        AtMatrix mtx = matrices ? matrices[k] : AiM4Identity();
        if (applyInvParentMtx)
            mtx = AiM4Mult(mtx, invParentMtx);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                m[i][j] = mtx[i][j];
            }
        }
        writer.SetAttribute(attr, GfMatrix4d(m), (hasMotion) ? &time : nullptr);
        time += timeDelta;
    }
    AiArrayUnmapConst(array);
}

static void processMaterialBinding(AtNode* shader, AtNode* displacement, UsdPrim& prim, UsdArnoldWriter& writer)
{
   
    // Special case : by default when no shader is assigned, the shader that is returned
    // is the arnold default shader "ai_default_reflection_shader". Since it's an implicit node that
    // isn't exported to arnold, we don't want to consider it
    static const std::string ai_default_reflection_shader = "ai_default_reflection_shader";
    if (shader && std::string(AiNodeGetName(shader)) == ai_default_reflection_shader) {
        shader = nullptr;
    }

    if (shader == nullptr && displacement == nullptr)
        return; // nothing to export

    const std::string scope = writer.GetScope();
    std::string mtlScope = scope + writer.GetMtlScope();
    writer.SetScope("");
    const std::string stripHierarchy = writer.GetStripHierarchy();

    std::string shaderName = (shader) ? UsdArnoldPrimWriter::GetArnoldNodeName(shader, writer) : "";
    std::string dispName = (displacement) ? UsdArnoldPrimWriter::GetArnoldNodeName(displacement, writer) : "";
    std::string materialName;    
    UsdShadeMaterial mat;

    // Find if there is an existing material binding to this direct primitive
    // i.e. without taking into account parent primitives #2501
    UsdRelationship matRel = UsdShadeMaterialBindingAPI(prim).GetDirectBindingRel();
    SdfPathVector matTargets;
    matRel.GetTargets(&matTargets);
    if (!matTargets.empty()) {
        mat = UsdShadeMaterial(writer.GetUsdStage()->GetPrimAtPath(matTargets[0]));
    }
    if (mat) {
        materialName = mat.GetPath().GetString();
    } else {
        // The material node doesn't exist in Arnold, but is required in USD,
        // let's create one based on the name of the shader plus the name of
        // the eventual displacement. This way we'll have a unique material in USD
        // per combination of surface shader + displacement instead of duplicating it
        // for every geometry.
        if (!shaderName.empty()) {
            // Ensure the "mtl" primitive is a scope
            writer.CreateScopeHierarchy(SdfPath(mtlScope));
            materialName = mtlScope + shaderName;
            if (!dispName.empty()) {
                size_t namePos = dispName.find_last_of('/');
                materialName += (namePos == std::string::npos) ? dispName : dispName.substr(namePos + 1);
            }
        } else {
            // no shader name was found, let's make up one based on the shape name
            materialName = prim.GetPath().GetString();
            materialName += "_material";
        }
        // Note that if the material was already created, Define will just return
        // the existing one
        mat = UsdShadeMaterial::Define(writer.GetUsdStage(), SdfPath(materialName));
        // Bind the material to this primitive
        UsdShadeMaterialBindingAPI::Apply(prim).Bind(mat);
    }

    // Now bind the eventual surface shader and displacement to the material.

    // Store the previous writer scope, and set a new one based on the material name.
    // This way, the surface and displacement shading trees that will be exported below
    // will be placed under the hierarchy of this material (see #1067). This means that
    // one arnold shader could eventually be duplicated in the usd file if he's used with
    // different displacement shaders. We also need to strip the material's parent hierarchy 
    // from each shader name, otherwise the scope might appear twice under the shaders
    
    writer.SetScope(materialName);
    std::string materialPath = TfGetPathName(shaderName);
    if (materialPath != "/")
        writer.SetStripHierarchy(materialPath);
    
    const TfToken arnoldContext("arnold");
    if (shader) {
        // Write the surface shader under the material's scope.
        // Here we only want to consider the last name in the prim 
        // hierarchy, so we're stripping the scope here
        writer.WritePrimitive(shader); 
        
        UsdShadeOutput surfaceOutput = mat.CreateSurfaceOutput(arnoldContext);
        // retrieve the new shader name (with the material scope applied)
        shaderName = UsdArnoldPrimWriter::GetArnoldNodeName(shader, writer);
        if (writer.GetUsdStage()->GetPrimAtPath(SdfPath(shaderName))) {
            // Connect the surface shader output to the material
            std::string surfaceTargetName = shaderName + std::string(".outputs:out");
            surfaceOutput.ConnectToSource(SdfPath(surfaceTargetName));
        }
    }
    if (displacement) {
        // write the surface shader under the material's scope
        writer.WritePrimitive(displacement); 
        UsdShadeOutput dispOutput = mat.CreateDisplacementOutput(arnoldContext);
        // retrieve the new shader name (with the material scope applied)
        dispName = UsdArnoldPrimWriter::GetArnoldNodeName(displacement, writer);
        if (writer.GetUsdStage()->GetPrimAtPath(SdfPath(dispName))) {
            // Connect the displacement shader output to the material
            std::string dispTargetName = dispName + std::string(".outputs:out");
            dispOutput.ConnectToSource(SdfPath(dispTargetName));
        }
    }
    // Restore the previous scope
    writer.SetScope(scope);
    // Eventually restore the previous stripHierarchy
    if (materialPath != "/")
        writer.SetStripHierarchy(stripHierarchy);
}

void UsdArnoldPrimWriter::_WriteMaterialBinding(
    const AtNode* node, UsdPrim& prim, UsdArnoldWriter& writer, AtArray* shidxsArray)
{

    if (!writer.GetWriteMaterialBindings() || !(writer.GetMask() & AI_NODE_SHADER))
        return;

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
        AtArray* shaders = AiNodeGetArray(node, AtString("shader"));
        static const AtString polymesh_str("polymesh");
        AtArray* displacements = (AiNodeIs(node, polymesh_str)) ? AiNodeGetArray(node, AtString("disp_map")) : nullptr;

        unsigned int numShaders = (shaders) ? AiArrayGetNumElements(shaders) : 0;
        unsigned int numDisp = (displacements) ? AiArrayGetNumElements(displacements) : 0;

        if (numShaders >= 1 || numDisp >= 1) {
            // Only create geom subsets if there is more than one shader / displacement.
            unsigned char* shidxs = (unsigned char*)AiArrayMap(shidxsArray);

            unsigned int numSubsets = AiMax(numShaders, numDisp);
            if (writer.GetAppendFile()) {
                // If we are in append mode, we want to check if the subsets already exist, in which case 
                // we don't want to create them again
                std::vector<UsdGeomSubset> prevSubsets = UsdGeomSubset::GetGeomSubsets(geom, _tokens->face);
                if (prevSubsets.size() >= numSubsets)
                   return; 
            }
            for (unsigned int i = 0; i < numSubsets; ++i) {
                AtNode* shader = (i < numShaders) ? (AtNode*)AiArrayGetPtr(shaders, i) : nullptr;
                AtNode* displacement = (i < numDisp) ? (AtNode*)AiArrayGetPtr(displacements, i) : nullptr;

                VtIntArray indices;
                // Append in this array all the indices that match the current shading subset
                for (int j = 0; j < (int) shidxsCount; ++j) {
                    if (shidxs[j] == i)
                        indices.push_back(j);
                }

                UsdGeomSubset subset = UsdGeomSubset::CreateUniqueGeomSubset(
                    geom, _tokens->subset,
                    _tokens->face, // currently the only supported type
                    indices,
                    _tokens->materialBind, 
                    _tokens->partition);
                UsdPrim subsetPrim = subset.GetPrim();

                // Process the material binding on the subset primitive
                processMaterialBinding(shader, displacement, subsetPrim, writer);
            }
            AiArrayUnmap(shidxsArray);
            return;
        }
    }

    //-- Single shader for the whole geometry
    AtNode* shader = (AtNode*)AiNodeGetPtr(node, AtString("shader"));
    static const AtString polymesh_str("polymesh");
    AtNode* displacement = (AiNodeIs(node, polymesh_str)) ? (AtNode*)AiNodeGetPtr(node, AtString("disp_map")) : nullptr;

    processMaterialBinding(shader, displacement, prim, writer);
}
