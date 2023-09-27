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
#include "utils.h"

#include <pxr/base/tf/getenv.h>

#include <pxr/base/gf/matrix4f.h>

#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <ai.h>
#include <constant_strings.h>
#include <unordered_map>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((filename, "arnold:filename"))
    ((output, "outputs:out"))
    ((outputsPrefix, "outputs:"))
    ((uigroups, "ui:groups"))
    (uimin)
    (uimax)
    (uisoftmin)
    (uisoftmax)
    (enumValues)
    (attrsOrder)
);
// clang-format on

namespace {

// TODO(pal): All this should be moved to a schema API.

// The conversion classes store both the sdf type and a simple function pointer
// that can do the conversion.
struct DefaultValueConversion {
    const SdfValueTypeName& type;
    using Convert = VtValue (*)(const AtParamValue&, const AtParamEntry*);
    Convert f = nullptr;

    template <typename F>
    DefaultValueConversion(const SdfValueTypeName& _type, F&& _f) : type(_type), f(std::move(_f))
    {
    }

    DefaultValueConversion() = delete;
};

struct ArrayConversion {
    const SdfValueTypeName& type;
    using Convert = VtValue (*)(const AtArray*);
    Convert f = nullptr;

    template <typename F>
    ArrayConversion(const SdfValueTypeName& _type, F&& _f) : type(_type), f(std::move(_f))
    {
    }

    ArrayConversion() = delete;
};

inline GfMatrix4d _ConvertMatrix(const AtMatrix& mat) { return GfMatrix4d(mat.data); }

inline GfMatrix4d _ArrayGetMatrix(const AtArray* arr, uint32_t i)
{
    const auto mat = AiArrayGetMtx(arr, i);
    return GfMatrix4d(mat.data);
}

// Most of the USD types line up with the arnold types, so memcpying is enough
// except for strings, so we need to be able to specialize for that case.
template <typename LHT, typename RHT>
inline void _Convert(LHT& l, const RHT& r)
{
    static_assert(sizeof(LHT) == sizeof(RHT), "Input data for convert must has the same size");
    memcpy(&l, &r, sizeof(r));
};

template <>
inline void _Convert<std::string, AtString>(std::string& l, const AtString& r)
{
    const auto* c = r.c_str();
    if (c != nullptr) {
        l = c;
    }
};

template <typename T, typename R = T>
inline VtValue _ExportArray(
    const AtArray* arr, R (*f)(
                            const AtArray*, uint32_t
                            ))
{
    // we already check the validity of the array before this call
    const auto nelements = AiArrayGetNumElements(arr);
    if (nelements == 0) {
        return VtValue(VtArray<T>());
    }
    VtArray<T> out_arr(nelements);
    for (auto i = 0u; i < nelements; ++i) {
        _Convert(
            out_arr[i], f(arr, i
                          ));
    }
    return VtValue(out_arr);
}

// While the type integers are continouos and we could use a vector of pairs
// using an unordered map makes sure we handle cases when a type is not implemented.
// We also don't have to make sure the order of the declarations are matching
// the values of the defines.
// TODO(pal): We could create a function that converts a generic initializer list
//  input to an ordered vector, and fills in any gaps. This would give us slightly
//  faster lookups, but unordered maps with a cheap hash (it is a no cost in this case)
//  are quite fast, even compared to a vector.
const std::unordered_map<uint8_t, DefaultValueConversion>& _DefaultValueConversionMap()
{
    const static std::unordered_map<uint8_t, DefaultValueConversion> ret{
        {AI_TYPE_BYTE,
         {SdfValueTypeNames->UChar,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue { return VtValue(pv.BYTE()); }}},
        {AI_TYPE_INT,
         {SdfValueTypeNames->Int,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue { return VtValue(pv.INT()); }}},
        {AI_TYPE_UINT,
         {SdfValueTypeNames->UInt,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue { return VtValue(pv.UINT()); }}},
        {AI_TYPE_BOOLEAN,
         {SdfValueTypeNames->Bool,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue { return VtValue(pv.BOOL()); }}},
        {AI_TYPE_FLOAT,
         {SdfValueTypeNames->Float,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue { return VtValue(pv.FLT()); }}},
        {AI_TYPE_RGB,
         {SdfValueTypeNames->Color3f,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue {
              const auto& v = pv.RGB();
              return VtValue(GfVec3f(v.r, v.g, v.b));
          }}},
        {AI_TYPE_RGBA,
         {SdfValueTypeNames->Color4f,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue {
              const auto& v = pv.RGBA();
              return VtValue(GfVec4f(v.r, v.g, v.b, v.a));
          }}},
        {AI_TYPE_VECTOR,
         {SdfValueTypeNames->Vector3f,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue {
              const auto& v = pv.VEC();
              return VtValue(GfVec3f(v.x, v.y, v.z));
          }}},
        {AI_TYPE_VECTOR2,
         {SdfValueTypeNames->Float2,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue {
              const auto& v = pv.VEC2();
              return VtValue(GfVec2f(v.x, v.y));
          }}},
        {AI_TYPE_STRING,
         {SdfValueTypeNames->String,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue { return VtValue(pv.STR().c_str()); }}},
        {AI_TYPE_POINTER, {SdfValueTypeNames->String, nullptr}},
        {AI_TYPE_NODE, {SdfValueTypeNames->String, nullptr}},
        {AI_TYPE_MATRIX,
         {SdfValueTypeNames->Matrix4d,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue { return VtValue(_ConvertMatrix(*pv.pMTX())); }}},
        {AI_TYPE_ENUM,
         {SdfValueTypeNames->String,
          [](const AtParamValue& pv, const AtParamEntry* pe) -> VtValue {
              if (pe == nullptr) {
                  return VtValue("");
              }
              const auto enums = AiParamGetEnum(pe);
              return VtValue(AiEnumGetString(enums, pv.INT()));
          }}},
        {AI_TYPE_CLOSURE, {SdfValueTypeNames->String, nullptr}},
        {AI_TYPE_USHORT,
         {SdfValueTypeNames->UInt,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue { return VtValue(pv.UINT()); }}},
        {AI_TYPE_HALF,
         {SdfValueTypeNames->Half,
          [](const AtParamValue& pv, const AtParamEntry*) -> VtValue { return VtValue(pv.FLT()); }}},
    };
    return ret;
}

const std::unordered_map<uint8_t, ArrayConversion>& _ArrayTypeConversionMap()
{
    const static std::unordered_map<uint8_t, ArrayConversion> ret{
        {AI_TYPE_BYTE,
         {SdfValueTypeNames->UCharArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<uint8_t>(a, AiArrayGetByte); }}},
        {AI_TYPE_INT,
         {SdfValueTypeNames->IntArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<int32_t>(a, AiArrayGetInt); }}},
        {AI_TYPE_UINT,
         {SdfValueTypeNames->UIntArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<uint32_t>(a, AiArrayGetUInt); }}},
        {AI_TYPE_BOOLEAN,
         {SdfValueTypeNames->BoolArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<bool>(a, AiArrayGetBool); }}},
        {AI_TYPE_FLOAT,
         {SdfValueTypeNames->FloatArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<float>(a, AiArrayGetFlt); }}},
        {AI_TYPE_RGB,
         {SdfValueTypeNames->Color3fArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<GfVec3f, AtRGB>(a, AiArrayGetRGB); }}},
        {AI_TYPE_RGBA,
         {SdfValueTypeNames->Color4fArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<GfVec4f, AtRGBA>(a, AiArrayGetRGBA); }}},
        {AI_TYPE_VECTOR,
         {SdfValueTypeNames->Vector3fArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<GfVec3f, AtVector>(a, AiArrayGetVec); }}},
        {AI_TYPE_VECTOR2,
         {SdfValueTypeNames->Float2Array,
          [](const AtArray* a) -> VtValue { return _ExportArray<GfVec2f, AtVector2>(a, AiArrayGetVec2); }}},
        {AI_TYPE_STRING,
         {SdfValueTypeNames->StringArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<std::string, AtString>(a, AiArrayGetStr); }}},
        {AI_TYPE_POINTER, {SdfValueTypeNames->StringArray, nullptr}},
        {AI_TYPE_NODE, {SdfValueTypeNames->StringArray, nullptr}},
        // Not supporting arrays of arrays. I don't think it's even possible
        // in the arnold core.
        {AI_TYPE_MATRIX,
         {SdfValueTypeNames->Matrix4dArray,
          [](const AtArray* a) -> VtValue {
              const auto nelements = AiArrayGetNumElements(a);
              VtArray<GfMatrix4d> arr(nelements);
              for (auto i = 0u; i < nelements; ++i) {
                  arr[i] = _ArrayGetMatrix(a, i);
              }
              return VtValue(arr);
          }}}, // TODO: implement
        {AI_TYPE_ENUM,
         {SdfValueTypeNames->IntArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<int32_t>(a, AiArrayGetInt); }}},
        {AI_TYPE_CLOSURE, {SdfValueTypeNames->StringArray, nullptr}},
        {AI_TYPE_USHORT,
         {SdfValueTypeNames->UIntArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<uint32_t>(a, AiArrayGetUInt); }}},
        {AI_TYPE_HALF,
         {SdfValueTypeNames->HalfArray,
          [](const AtArray* a) -> VtValue { return _ExportArray<float>(a, AiArrayGetFlt); }}},
    };
    return ret;
}

// These two utility functions either return a nullptr if the type is not supported
// or the pointer to the conversion struct.
const DefaultValueConversion* _GetDefaultValueConversion(uint8_t type)
{
    const auto& dvcm = _DefaultValueConversionMap();
    const auto it = dvcm.find(type);
    if (it != dvcm.end()) {
        return &it->second;
    } else {
        return nullptr;
    }
}

static bool _StrEndsWith(const std::string &str, const std::string &suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}
const ArrayConversion* _GetArrayConversion(uint8_t type)
{
    const auto& atm = _ArrayTypeConversionMap();
    const auto it = atm.find(type);
    if (it != atm.end()) {
        return &it->second;
    } else {
        return nullptr;
    }
}

VtValue _ReadArnoldMetadata(const AtMetaDataEntry *metadata)
{
    if (metadata == nullptr)
        return VtValue();
    // Get the metadata value
    switch(metadata->type) {
        case AI_TYPE_INT:
           return VtValue(metadata->value.INT());
        case AI_TYPE_BYTE:
           return VtValue(metadata->value.BYTE());
        case AI_TYPE_UINT:
           return VtValue(metadata->value.UINT());
        case AI_TYPE_BOOLEAN:
           return VtValue(metadata->value.BOOL());
        case AI_TYPE_FLOAT:
           return VtValue(metadata->value.FLT());
        case AI_TYPE_RGB:
           return VtValue(metadata->value.RGB());
        case AI_TYPE_RGBA:
           return VtValue(metadata->value.RGBA());
        case AI_TYPE_VECTOR:
           return VtValue(metadata->value.VEC());
        case AI_TYPE_VECTOR2:
           return VtValue(metadata->value.VEC2());
        case AI_TYPE_STRING:
           return VtValue(metadata->value.STR().c_str());
        default:
           break;
    }
    return VtValue();
}
// TODO(pal): We could also setup a metadata to store the raw arnold type,
//  for cases where multiple arnold types map to a single sdf type.
void _ReadArnoldShaderDef(UsdStageRefPtr stage, const AtNodeEntry* nodeEntry)
{    
    VtDictionary primCustomData;
    bool hide = false;
    // Get all metadatas for this shader
    AtMetaDataIterator* nodeMetadataIter = AiNodeEntryGetMetaDataIterator(nodeEntry);
    while(!AiMetaDataIteratorFinished(nodeMetadataIter)) {
        const AtMetaDataEntry* metadata = AiMetaDataIteratorGetNext(nodeMetadataIter);
        std::string metadataName(metadata->name.c_str());
        if (metadataName.empty())
            continue;
          
        TfToken usdPrimMetadata(metadataName.c_str());

        if (metadata->type == AI_TYPE_STRING && 
             (_StrEndsWith(metadataName, ".classification") ||
              _StrEndsWith(metadataName, ".category"))) {
            usdPrimMetadata = SdrPropertyMetadata->Role;
           
        } else if (metadata->name == str::ui_groups) {
            usdPrimMetadata = _tokens->uigroups;
        } else if (metadata->type == AI_TYPE_BOOLEAN && 
          (metadata->name == str::hide || _StrEndsWith(metadataName, ".hide"))) {
            hide |= (bool)metadata->value.BOOL();
            continue;          
        } else if (metadata->type == AI_TYPE_STRING && metadata->name == str::dcc) {
            AtString dcc = metadata->value.STR();
            if (!dcc.empty() && dcc != str::usd)
                continue;
        } else if (metadata->name == str::desc) {
            usdPrimMetadata = SdrPropertyMetadata->Help;
        }
        primCustomData[usdPrimMetadata] = _ReadArnoldMetadata(metadata);
    }
    AiMetaDataIteratorDestroy(nodeMetadataIter);

    if (hide)
        return;

    auto prim = stage->DefinePrim(SdfPath(TfStringPrintf("/%s", AiNodeEntryGetName(nodeEntry))));
    const auto filename = AiNodeEntryGetFilename(nodeEntry);
    prim.SetMetadata(_tokens->filename, VtValue(TfToken(filename == nullptr ? "<built-in>" : filename)));
 
    // For shaders, we want to add an attribute for the output type
    if (AiNodeEntryGetType(nodeEntry) == AI_NODE_SHADER) {
        const int nbOutputs = AiNodeEntryGetNumOutputs(nodeEntry);
        if (nbOutputs <= 1) {
            const auto* conversion = _GetDefaultValueConversion(AiNodeEntryGetOutputType(nodeEntry));
            if (conversion != nullptr) {
                prim.CreateAttribute(_tokens->output, conversion->type, false);
            }
        } else {
            for (int outIndex = 0; outIndex < nbOutputs; outIndex++) {
                const AtParamEntry *outEntry = AiNodeEntryGetOutput(nodeEntry, outIndex);
                const auto* conversion = _GetDefaultValueConversion(AiParamGetType(outEntry));
                if (conversion != nullptr) {
                    TfToken outputName(_tokens->outputsPrefix.GetString() + std::string(AiParamGetName(outEntry)));
                    prim.CreateAttribute(outputName, conversion->type, false);
                }
            }
        }
    } else if (AiNodeEntryGetType(nodeEntry) == AI_NODE_DRIVER) {
        // create an output type for imagers
        prim.CreateAttribute(_tokens->output, SdfValueTypeNames->String, false);
    }

    VtArray<std::string> attrsOrder;
    auto paramIter = AiNodeEntryGetParamIterator(nodeEntry);
 
    while (!AiParamIteratorFinished(paramIter)) {
        const auto* pentry = AiParamIteratorGetNext(paramIter);
        const auto paramType = AiParamGetType(pentry);
        const AtString paramName = AiParamGetName(pentry);
        if (paramName.empty() || paramName == str::name)
            continue;
        
        UsdAttribute attr;
        VtDictionary customData;
        
        if (paramType == AI_TYPE_ARRAY) {
            const auto* defaultValue = AiParamGetDefault(pentry);
            if (defaultValue == nullptr) {
                continue;
            }
            const auto* array = defaultValue->ARRAY();
            if (array == nullptr) {
                continue;
            }
            const auto elemType = AiArrayGetType(array);
            const auto* conversion = _GetArrayConversion(elemType);
            if (conversion == nullptr) {
                continue;
            }
            attr = prim.CreateAttribute(TfToken(paramName.c_str()), conversion->type, false);

            if (conversion->f != nullptr) {
                attr.Set(conversion->f(array));
            }
        } else {

            const auto* conversion = _GetDefaultValueConversion(paramType);
            if (conversion == nullptr) {
                continue;
            }
            attr = prim.CreateAttribute(TfToken(paramName.c_str()), conversion->type, false);

            if (conversion->f != nullptr) {
                attr.Set(conversion->f(*AiParamGetDefault(pentry), pentry));
            }          
        }
        attrsOrder.push_back(paramName.c_str());

        // For enum attributes, get all the allowed enum values and
        // set them as customData through the metadata "enumValues"
        if (paramType == AI_TYPE_ENUM) {
            VtStringArray enumValues;
            for (int ei=0 ;; ei++)
            {
                const char* enumStr = AiEnumGetString(AiParamGetEnum(pentry), ei);
                if (!enumStr)
                   break;
               enumValues.push_back(enumStr);
            }
            customData[_tokens->enumValues] = enumValues;
        }

        bool foundLabel = false;
        // Get all metadatas for this attribute
        AtMetaDataIterator* metadataIter = AiNodeEntryGetMetaDataIterator(nodeEntry, paramName);
        while(!AiMetaDataIteratorFinished(metadataIter)) {
            const AtMetaDataEntry* metadata = AiMetaDataIteratorGetNext(metadataIter);
            if (!metadata)
                continue;

            TfToken usdMetadata;
            if (metadata->name == str::linkable)
                usdMetadata = SdrPropertyMetadata->Connectable;
            else if (metadata->name == str::_min)
                usdMetadata = _tokens->uimin;
            else if (metadata->name == str::_max)
                usdMetadata = _tokens->uimax;
            else if (metadata->name == str::softmin)
                usdMetadata = _tokens->uisoftmin;
            else if (metadata->name == str::softmax)
                usdMetadata = _tokens->uisoftmax;
            else if (metadata->name == str::label) {
                usdMetadata = SdrPropertyMetadata->Label;
                foundLabel = true;
            } else if (metadata->name == str::desc)
                usdMetadata = SdrPropertyMetadata->Help;
            else if (metadata->name == str::path && 
                    metadata->value.STR() == str::file) {
                // In arnold some string attributes should actually represent
                // an asset attribute in USD. They have a metadata "path" set to "file".
                // USD expects such attributes to be declared as strings in the 
                // with a metadata IsAssetIdentifier set to true
                customData[SdrPropertyMetadata->IsAssetIdentifier] = VtValue(true);
                continue;                
            }
            else
                usdMetadata = TfToken(metadata->name.c_str());

            customData[usdMetadata] = _ReadArnoldMetadata(metadata);
        }
        AiMetaDataIteratorDestroy(metadataIter);
        
        // If no "label" metadata is found for this attribute, we want to make one.
        // e.g. base_color => "Base Color"
        if (!foundLabel) {
            std::string attrLabel(paramName.c_str());
            bool capitalize = true;
            for (size_t i = 0; i < attrLabel.length(); ++i) {
                if (attrLabel[i] == '_') {
                    capitalize = true;
                    attrLabel[i] = ' ';
                } else {
                    if (capitalize)
                        attrLabel[i] = toupper(attrLabel[i]);
                    capitalize = false;
                }
            }
            customData[SdrPropertyMetadata->Label] = attrLabel.c_str();
        }
        attr.SetCustomData(customData);
    }
    AiParamIteratorDestroy(paramIter);
    
    // set attrsOrder as customData
    primCustomData[_tokens->attrsOrder] = attrsOrder;
    prim.SetCustomData(primCustomData);

}

} // namespace

UsdStageRefPtr NdrArnoldGetShaderDefs()
{
    // This is guaranteed to be thread safe by the C++11 standard.
    // It's also using a pretty fast lock guard, so checking the value
    // is cheap. We also don't want to initiate global variables, as those
    // could cause thread locks withing USD when initalizing libraries in
    // an unusual order.
    static auto ret = []() -> UsdStageRefPtr {
        auto stage = UsdStage::CreateInMemory("__ndrArnoldShaderDefs.usda");

        // We expect the existing arnold universe to load the plugins.
#if ARNOLD_VERSION_NUM >= 70100
        const auto hasActiveUniverse = AiArnoldIsActive();
#else
        const auto hasActiveUniverse = AiUniverseIsActive();   
#endif
        if (!hasActiveUniverse) {
            AiBegin(AI_SESSION_BATCH);
#if ARNOLD_VERSION_NUM >= 70100
            AiMsgSetConsoleFlags(nullptr, AI_LOG_NONE);
#else
            AiMsgSetConsoleFlags(AI_LOG_NONE);
#endif
        }

        auto* nodeIter = AiUniverseGetNodeEntryIterator(AI_NODE_SHADER | AI_NODE_DRIVER);

        while (!AiNodeEntryIteratorFinished(nodeIter)) {

            // filter out drivers that are not imagers
            auto* nodeEntry = AiNodeEntryIteratorGetNext(nodeIter);
            static const AtString s_subtype("subtype");
            AtString subtype;
            if ((AiNodeEntryGetType(nodeEntry) == AI_NODE_DRIVER))
            {
                if (!AiMetaDataGetStr(nodeEntry, AtString(), s_subtype, &subtype) || strcmp(subtype.c_str(), "imager"))
                    continue;
            }
            _ReadArnoldShaderDef(stage, nodeEntry);
        }
        AiNodeEntryIteratorDestroy(nodeIter);

        if (!hasActiveUniverse) {
            AiEnd();
        }

        return stage;
    }();
    return ret;
}

PXR_NAMESPACE_CLOSE_SCOPE
