//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "api_adapter.h"
#include <pxr/usd/usd/prim.h>
#include <ai.h>
#include <string>
#include "timesettings.h"
#include "api_adapter.h"

#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/vt/array.h>


PXR_NAMESPACE_USING_DIRECTIVE

class PrimvarsRemapper
{
public:
    PrimvarsRemapper() {}
    virtual ~PrimvarsRemapper() {}  

    virtual bool RemapValues(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
        VtValue &value);
    virtual bool RemapIndexes(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
        std::vector<unsigned int> &indexes);
    virtual void RemapPrimvar(TfToken &name, std::string &interpolation);
};

class InputAttribute {
public:
    InputAttribute() {}
    
    virtual const UsdAttribute* GetAttr() const { return nullptr; }
    virtual bool Get(VtValue *value, double time) {return false;}
    const SdfPathVector &GetConnections() const {return _connections;}
    virtual bool IsAnimated() const {return false;}

protected:
    SdfPathVector _connections;
};

class InputUsdAttribute : public InputAttribute
{
public:
    InputUsdAttribute(const UsdAttribute& attribute) : 
        _attr(attribute), InputAttribute()
    {
        if (_attr.HasAuthoredConnections())
            _attr.GetConnections(&_connections);

    }
    const UsdAttribute* GetAttr() const override { return &_attr; }
    bool Get(VtValue *value, double time) override
    {
        return value ? _attr.Get(value, time) : false;
    }
    bool IsAnimated() const override {return _attr.ValueMightBeTimeVarying();}

protected:
    const UsdAttribute &_attr;
};
class InputUsdPrimvar : public InputAttribute
{
public:
    InputUsdPrimvar(const UsdGeomPrimvar& primvar,
        bool computeFlattened = false, PrimvarsRemapper *primvarsRemapper = nullptr, 
        TfToken primvarInterpolation = TfToken()) :
        _primvar(primvar),
        _computeFlattened(computeFlattened),
        _primvarsRemapper(primvarsRemapper),
        _primvarInterpolation(primvarInterpolation),
        InputAttribute()
    {        
        const UsdAttribute &attr = _primvar.GetAttr();
        if (attr.HasAuthoredConnections())
            attr.GetConnections(&_connections);
    }

    bool Get(VtValue *value, double time) override
    {
        if (value == nullptr)
            return false;

        bool res = false;
        if (_computeFlattened)
            res = _primvar.ComputeFlattened(value, time);
        else
            res = _primvar.Get(value, time);
        
        if (_primvarsRemapper)
            _primvarsRemapper->RemapValues(_primvar, _primvarInterpolation, *value);

        return res;
    }

    const UsdAttribute* GetAttr() const override { return &(_primvar.GetAttr()); }
    bool IsAnimated() const override {return _primvar.GetAttr().ValueMightBeTimeVarying();}

protected:
    const UsdGeomPrimvar &_primvar;
    bool _computeFlattened = false;
    PrimvarsRemapper *_primvarsRemapper = nullptr;
    TfToken _primvarInterpolation;


};
/** Read String arrays, and handle the conversion from std::string / TfToken to AtString.
 */
inline
size_t ReadStringArray(InputAttribute &attr, AtNode *node, const char *attrName, const TimeSettings &time)
{
    // Strings can be represented in USD as std::string, TfToken or SdfAssetPath.
    // We'll try to get the input attribute value as each of these types
    AtArray *outArray = nullptr;
    size_t size;

    VtValue arrayValue;
    if (attr.Get(&arrayValue, time.frame)) {
        if (arrayValue.IsHolding<VtArray<std::string>>()) {
            const VtArray<std::string> &arrayStr = arrayValue.UncheckedGet<VtArray<std::string>>();
            size = arrayStr.size();
            if (size > 0) {
                outArray = AiArrayAllocate(size, 1, AI_TYPE_STRING);
                for (size_t i = 0; i < size; ++i) {
                    if (!arrayStr[i].empty())
                        AiArraySetStr(outArray, i, AtString(arrayStr[i].c_str()));
                    else
                        AiArraySetStr(outArray, i, AtString(""));
                }
            }
        } else if (arrayValue.IsHolding<VtArray<TfToken>>()) {
            const VtArray<TfToken> &arrayToken = arrayValue.UncheckedGet<VtArray<TfToken>>();
            size = arrayToken.size();
            if (size > 0) {
                outArray = AiArrayAllocate(size, 1, AI_TYPE_STRING);
                for (size_t i = 0; i < size; ++i) {
                    if (!arrayToken[i].GetString().empty())
                        AiArraySetStr(outArray, i, AtString(arrayToken[i].GetText()));
                    else
                        AiArraySetStr(outArray, i, AtString(""));
                }
            }
        } else if (arrayValue.IsHolding<VtArray<SdfAssetPath>>()) {
            const VtArray<SdfAssetPath> &arrayPath = arrayValue.UncheckedGet<VtArray<SdfAssetPath>>();
            size = arrayPath.size();
            if (size > 0) {
                outArray = AiArrayAllocate(size, 1, AI_TYPE_STRING);
                for (size_t i = 0; i < size; ++i) {
                    if (!arrayPath[i].GetResolvedPath().empty())
                        AiArraySetStr(outArray, i, AtString(arrayPath[i].GetResolvedPath().c_str()));
                    else
                        AiArraySetStr(outArray, i, AtString(""));
                }
            }
        }
    }

    if (outArray)
        AiNodeSetArray(node, AtString(attrName), outArray);
    else
        AiNodeResetParameter(node, AtString(attrName));

    return 1; // return the amount of motion keys
}


void ValidatePrimPath(std::string &path, const UsdPrim &prim);

void ReadAttribute(InputAttribute &attr, AtNode *node, const std::string &arnoldAttr, const TimeSettings &time,
    ArnoldAPIAdapter &context, int paramType, int arrayType = AI_TYPE_NONE, 
    const UsdPrim *prim = nullptr);

void ReadPrimvars(
        const UsdPrim &prim, AtNode *node, const TimeSettings &time, ArnoldAPIAdapter &context,
        PrimvarsRemapper *primvarsRemapper = nullptr);


void ReadArnoldParameters(
        const UsdPrim &prim, ArnoldAPIAdapter &context, AtNode *node, const TimeSettings &time,
        const std::string &scope = "arnold");

void _ReadArrayLink(
        const UsdPrim &prim, const UsdAttribute &attr, const TimeSettings &time, 
        ArnoldAPIAdapter &context, AtNode *node, const std::string &scope);

void _ReadAttributeConnection(
            const UsdPrim &prim, const SdfPathVector &connections, AtNode *node, const std::string &arnoldAttr,  
            const TimeSettings &time, ArnoldAPIAdapter &context, int paramType);

bool HasAuthoredAttribute(const UsdPrim &prim, const TfToken &attrName);bool HasAuthoredAttribute(const UsdPrim &prim, const TfToken &attrName);


static inline std::string _VtValueResolvePath(const SdfAssetPath& assetPath, const InputAttribute* inputAttr = nullptr)
{
    std::string path = assetPath.GetResolvedPath();
    if (path.empty()) {
        path = assetPath.GetAssetPath();
        // If the filename has tokens ("<UDIM>") and is relative, USD won't resolve it and we end up here.
        // In this case we need to resolve the path to pass to arnold ourselves, by looking at the composition arcs in
        // this primitive. Note that we only need this for UsdUvTexture attribute "inputs:file"
        const UsdAttribute *attr = inputAttr ? inputAttr->GetAttr() : nullptr;
        if (attr != nullptr && attr->GetName().GetString() == "inputs:file" && !path.empty() && TfIsRelativePath(path)) {
            UsdPrim prim = attr->GetPrim();
            if (prim && prim.IsA<UsdShadeShader>()) {
                UsdShadeShader shader(prim);
                TfToken id;
                shader.GetIdAttr().Get(&id);
                std::string shaderId = id.GetString();
                if (shaderId == "UsdUVTexture") {
                    // SdfComputeAssetPathRelativeToLayer returns search paths (vs anchored paths) unmodified,
                    // this is apparently to make sure they will be always searched again.
                    // This is not what we want, so we make sure the path is anchored
                    if (TfIsRelativePath(path) && path[0] != '.') {
                        path = "./" + path;
                    }
                    for (const auto& sdfProp : attr->GetPropertyStack()) {
                        const auto& layer = sdfProp->GetLayer();
                        if (layer && !layer->GetRealPath().empty()) {
                            std::string layerPath = SdfComputeAssetPathRelativeToLayer(layer, path);
                            if (!layerPath.empty() && layerPath != path &&
                                TfPathExists(layerPath.substr(0, layerPath.find_last_of("\\/")))) {
                                return layerPath;
                            }
                        }
                    }
                }
            }
        }
    }
    return path;
}


// AtString requires conversion and can't be trivially copied.
template <typename CastTo, typename CastFrom>
inline void _ConvertTo(CastTo& to, const CastFrom& from, const InputAttribute *attr = nullptr)
{
    to = static_cast<CastTo>(from);
}

template <>
inline void _ConvertTo<AtString, std::string>(AtString& to, const std::string& from, const InputAttribute *attr)
{
    to = AtString{from.c_str()};
}

template <>
inline void _ConvertTo<AtString, TfToken>(AtString& to, const TfToken& from, const InputAttribute *attr)
{
    to = AtString{from.GetText()};
}

template <>
inline void _ConvertTo<AtString, SdfAssetPath>(AtString& to, const SdfAssetPath& from, const InputAttribute *attr)
{
    std::string resolvedPath = _VtValueResolvePath(from, attr);
    to = AtString{resolvedPath.c_str()};
}

template <>
inline void _ConvertTo<std::string, std::string>(std::string& to, const std::string& from, const InputAttribute *attr)
{
    to = from;
}

template <>
inline void _ConvertTo<std::string, TfToken>(std::string& to, const TfToken& from, const InputAttribute *attr)
{
    to = std::string{from.GetText()};
}

template <>
inline void _ConvertTo<std::string, SdfAssetPath>(std::string& to, const SdfAssetPath& from, const InputAttribute *attr)
{    
    to = _VtValueResolvePath(from, attr);
}

template <>
inline void _ConvertTo<AtMatrix, GfMatrix4f>(AtMatrix& to, const GfMatrix4f& from, const InputAttribute *attr)
{
    const float* array = from.GetArray();
    memcpy(&to.data[0][0], array, 16 * sizeof(float));
}
template <>
inline void _ConvertTo<AtMatrix, GfMatrix4d>(AtMatrix& to, const GfMatrix4d& from, const InputAttribute *attr)
{
    // rely on GfMatrix conversions
    GfMatrix4f gfMatrix(from);
    const float* array = gfMatrix.GetArray();
    memcpy(&to.data[0][0], array, 16 * sizeof(float));
}

template <typename CastTo, typename CastFrom>
inline bool _VtValueGet(const VtValue& value, CastTo& data, const InputAttribute *attr = nullptr)
{    
    using CastFromType = typename std::remove_cv<typename std::remove_reference<CastFrom>::type>::type;
    using CastToType = typename std::remove_cv<typename std::remove_reference<CastTo>::type>::type;
    if (value.IsHolding<CastFromType>()) {
        _ConvertTo(data, value.UncheckedGet<CastFromType>(), attr);
        return true;
    } else if (value.IsHolding<VtArray<CastFromType>>()) {
        const auto& arr = value.UncheckedGet<VtArray<CastFromType>>();
        if (!arr.empty()) {
            _ConvertTo(data, arr[0], attr);
            return true;
        }
    }
    return false;
}

template <typename CastTo>
inline bool _VtValueGetRecursive(const VtValue& value, CastTo& data, const InputAttribute *attr = nullptr)
{
    return false;
}
template <typename CastTo, typename CastFrom, typename... CastRemaining>
inline bool _VtValueGetRecursive(const VtValue& value, CastTo& data, const InputAttribute *attr = nullptr)
{
    return _VtValueGet<CastTo, CastFrom>(value, data, attr) || 
           _VtValueGetRecursive<CastTo, CastRemaining...>(value, data, attr);
}

template <typename CastTo, typename CastFrom, typename... CastRemaining>
inline bool VtValueGet(const VtValue& value, CastTo& data, const InputAttribute *attr = nullptr)
{    
    return _VtValueGet<CastTo, CastTo>(value, data, attr) || 
           _VtValueGetRecursive<CastTo, CastFrom, CastRemaining...>(value, data, attr);
}

static inline bool VtValueGetBool(const VtValue& value, bool defaultValue = false)
{   
    if (!value.IsEmpty())
        VtValueGet<bool, int, unsigned int, char, unsigned char, long, unsigned long>(value, defaultValue);
    return defaultValue;
}

static inline float VtValueGetFloat(const VtValue& value, float defaultValue = 0.f)
{
    if (!value.IsEmpty())
        VtValueGet<float, double, GfHalf>(value, defaultValue);

    return defaultValue;
}

static inline unsigned char VtValueGetByte(const VtValue& value, unsigned char defaultValue = 0)
{
    if (!value.IsEmpty())
        VtValueGet<unsigned char, int, unsigned int, uint8_t, char, long, unsigned long>(value, defaultValue);

    return defaultValue;
}

static inline int VtValueGetInt(const VtValue& value, int defaultValue = 0)
{
    if (!value.IsEmpty())
        VtValueGet<int, long, unsigned int, unsigned char, char, unsigned long>(value, defaultValue);

    return defaultValue;
}

static inline unsigned int VtValueGetUInt(const VtValue& value, unsigned int defaultValue = 0)
{
    if (!value.IsEmpty())
        VtValueGet<unsigned int, int, unsigned char, char, unsigned long, long>(value, defaultValue);

    return defaultValue;
}

static inline GfVec2f VtValueGetVec2f(const VtValue& value, GfVec2f defaultValue = GfVec2f(0.f))
{
    if (!value.IsEmpty())
        VtValueGet<GfVec2f, GfVec2d, GfVec2h>(value, defaultValue);

    return defaultValue;
}

static inline GfVec3f VtValueGetVec3f(const VtValue& value, GfVec3f defaultValue = GfVec3f(0.f))
{
    if (value.IsEmpty())
        return defaultValue;

    if (!VtValueGet<GfVec3f, GfVec3d, GfVec3h>(value, defaultValue)) {
        GfVec4f vec4(0.f);
        if (VtValueGet<GfVec4f, GfVec4d, GfVec4h>(value, vec4))
            defaultValue = GfVec3f(vec4[0], vec4[1], vec4[2]);
    }
    return defaultValue;
}

static inline GfVec4f VtValueGetVec4f(const VtValue& value, GfVec4f defaultValue = GfVec4f(0.f))
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


static inline std::string VtValueGetString(const VtValue& value, const InputAttribute *attr = nullptr)
{
    std::string result;
    if (value.IsEmpty())
        return result;
    
    VtValueGet<std::string, TfToken, SdfAssetPath>(value, result, attr);
    return result;
}

static inline AtMatrix VtValueGetMatrix(const VtValue& value)
{
    if (value.IsEmpty())
        return AiM4Identity();
    AtMatrix result;
    _VtValueGetRecursive<AtMatrix, GfMatrix4f, GfMatrix4d>(value, result, nullptr);
    return result;
}



/////////////////////////

template <typename CastTo, typename CastFrom>
inline AtArray *_VtValueGetArray(const std::vector<VtValue>& values, uint8_t arnoldType, InputAttribute *attr = nullptr)
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
            _ConvertTo(*arrayData, v);
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
                    _ConvertTo(*arrayData, vElem, attr);
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
inline AtArray *_VtValueGetArrayRecursive(const std::vector<VtValue>& values, uint8_t arnoldType, InputAttribute *attr = nullptr)
{
    return nullptr;
}
template <typename CastTo, typename CastFrom, typename... CastRemaining>
inline AtArray *_VtValueGetArrayRecursive(const std::vector<VtValue>& values, uint8_t arnoldType, InputAttribute *attr = nullptr)
{
    AtArray *arr = _VtValueGetArray<CastTo, CastFrom>(values, arnoldType, attr);
    if (arr != nullptr)
        return arr;

    return _VtValueGetArrayRecursive<CastTo, CastRemaining...>(values, arnoldType, attr);
}

template <typename CastTo, typename CastFrom, typename... CastRemaining>
inline AtArray *VtValueGetArray(const std::vector<VtValue>& values, uint8_t arnoldType, InputAttribute *attr = nullptr)
{   
    AtArray *arr = _VtValueGetArray<CastTo, CastTo>(values, arnoldType, attr);
    if (arr !=  nullptr)
        return arr;

    return _VtValueGetArrayRecursive<CastTo, CastFrom, CastRemaining...>(values, arnoldType, attr);
}

static inline AtArray *VtValueGetArray(const std::vector<VtValue>& values, uint8_t arnoldType, 
    ArnoldAPIAdapter &context, InputAttribute *attr = nullptr)
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
            return _VtValueGetArrayRecursive<AtString, std::string, TfToken, SdfAssetPath>(values, arnoldType, attr);
        default:
            break;
    }
    return nullptr;

}





































bool ReadArrayAttribute(InputAttribute& attr, AtNode* node, const char* attrName, const TimeSettings& time, 
    ArnoldAPIAdapter &context, uint8_t arrayType = AI_TYPE_NONE);

template <class U, class A>
size_t ReadArray(
    UsdAttribute attr, AtNode* node, const char* attrName, const TimeSettings& time, uint8_t attrType = AI_TYPE_NONE)
{
    InputUsdAttribute inputAttr(attr);
    return ReadArray<U, A>(inputAttr, node, attrName, time, attrType);
}
/** Convert a USD array attribute (type U), to an Arnold array (type A).
 *  When both types are identical, we can simply their pointer to create the
 *array. Otherwise we need to copy the data first
 **/
template <class U, class A>
size_t ReadArray(
    InputAttribute& attr, AtNode* node, const char* attrName, const TimeSettings& time, uint8_t attrType = AI_TYPE_NONE)
{
    bool sameData = std::is_same<U, A>::value;
    const UsdAttribute* usdAttr = attr.GetAttr();

    if (attrType == AI_TYPE_NONE) {
        if (std::is_same<A, float>::value)
            attrType = AI_TYPE_FLOAT;
        else if (std::is_same<A, int>::value)
            attrType = AI_TYPE_INT;
        else if (std::is_same<A, bool>::value)
            attrType = AI_TYPE_BOOLEAN;
        else if (std::is_same<A, unsigned int>::value)
            attrType = AI_TYPE_UINT;
        else if (std::is_same<A, unsigned char>::value)
            attrType = AI_TYPE_BYTE;
        else if (std::is_same<A, GfVec3f>::value)
            attrType = AI_TYPE_VECTOR;
        else if (std::is_same<A, AtRGB>::value)
            attrType = AI_TYPE_RGB;
        else if (std::is_same<A, AtRGBA>::value)
            attrType = AI_TYPE_RGBA;
        else if (std::is_same<A, GfVec4f>::value)
            attrType = AI_TYPE_RGBA;
        else if (std::is_same<A, TfToken>::value)
            attrType = AI_TYPE_STRING;
        else if (std::is_same<A, std::string>::value)
            attrType = AI_TYPE_STRING;
        else if (std::is_same<A, GfMatrix4f>::value)
            attrType = AI_TYPE_MATRIX;
        else if (std::is_same<A, GfMatrix4d>::value)
            attrType = AI_TYPE_MATRIX;
        else if (std::is_same<A, AtMatrix>::value) {
            if (std::is_same<U, GfMatrix4f>::value)
                sameData = true;
            attrType = AI_TYPE_MATRIX;
        } else if (std::is_same<A, AtVector>::value) {
            attrType = AI_TYPE_VECTOR;
            if (std::is_same<U, GfVec3f>::value) // AtVector is represented the same
                                                 // way as GfVec3f
                sameData = true;
        } else if (std::is_same<A, GfVec2f>::value)
            attrType = AI_TYPE_VECTOR2;
        else if (std::is_same<A, AtVector2>::value) {
            attrType = AI_TYPE_VECTOR2;
            if (std::is_same<U, GfVec2f>::value) // AtVector2 is represented the
                                                 // same way as GfVec2f
                sameData = true;
        }
    }

    // Call a dedicated function for string conversions
    if (attrType == AI_TYPE_STRING)
        return ReadStringArray(attr, node, attrName, time);

    bool animated = time.motionBlur && usdAttr->ValueMightBeTimeVarying();

    if (!animated) {
        // Single-key arrays
        VtValue val;
        if (!attr.Get(&val, time.frame)) {
            // Create an empty array
            AiNodeSetArray(node, AtString(attrName), AiArrayConvert(0, 1, attrType, nullptr));
            return 0;
        }
        const VtArray<U>* array = &(val.Get<VtArray<U>>());

        size_t size = array->size();
        if (size > 0) {
            if (std::is_same<U, GfMatrix4d>::value) {
                // special case for matrices. They're single
                // precision in arnold but double precision in USD,
                // and there is no copy from one to the other.
                VtArray<GfMatrix4d>* arrayMtx = (VtArray<GfMatrix4d>*)(array);
                GfMatrix4d* matrices = arrayMtx->data();
                std::vector<AtMatrix> arnoldVec(size);
                for (size_t v = 0; v < size; ++v) {
                    AtMatrix& aiMat = arnoldVec[v];
                    const double* matArray = matrices[v].GetArray();
                    for (unsigned int i = 0; i < 4; ++i)
                        for (unsigned int j = 0; j < 4; ++j)
                            aiMat[i][j] = matArray[4 * i + j];
                }
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, 1, AI_TYPE_MATRIX, &arnoldVec[0]));

            } else if (sameData) {
                // The USD data representation is the same as the Arnold one, we don't
                // need to convert the data
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, 1, attrType, array->cdata()));
            } else {
                // Different data representation between USD and Arnold, we need to
                // copy the vector. 
                VtArray<A> arnold_vec;
                arnold_vec.reserve(array->size());
                for (const auto &elem : (*array))
                    arnold_vec.push_back(static_cast<A>(elem));
                
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, 1, attrType, arnold_vec.cdata()));
            }
        } else
            AiNodeResetParameter(node, AtString(attrName));

        return 1; // return the amount of keys
    } else {
        // Animated array
        GfInterval interval(time.start(), time.end(), false, false);
        std::vector<double> timeSamples;
        usdAttr->GetTimeSamplesInInterval(interval, &timeSamples);
        // need to add the start end end keys (interval has open bounds)
        size_t numKeys = timeSamples.size() + 2;

        double timeStep = double(interval.GetMax() - interval.GetMin()) / double(numKeys - 1);
        double timeVal = interval.GetMin();

        VtValue val;
        if (!attr.Get(&val, timeVal)) {
            // Create an empty array
            AiNodeSetArray(node, AtString(attrName), AiArrayConvert(0, 1, attrType, nullptr));
            return 0;
        }

        const VtArray<U>* array = &(val.Get<VtArray<U>>());

        // Arnold arrays don't support varying element counts per key.
        // So if we find that the size changes over time, we will just take a single key for the current frame        
        size_t size = array->size();
        if (size == 0)
            return 0;
        
        if (std::is_same<U, GfMatrix4d>::value) {
            VtArray<AtMatrix> arnoldVec(size * numKeys);
            int index = 0;

            for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
                if (i > 0) {
                    // if a time sample is missing, we can't translate 
                    // this attribute properly
                    if (!attr.Get(&val, timeVal))
                        return 0;
                    
                    array = &(val.Get<VtArray<U>>());
                }
                VtArray<GfMatrix4d>* arrayMtx = (VtArray<GfMatrix4d>*)(array);
                GfMatrix4d* matrices = arrayMtx->data();
                if (arrayMtx->size() != size) {
                    // Arnold won't support varying element count. 
                    // We need to only consider a single key corresponding to the current frame
                    arnoldVec.clear();
                    if (!attr.Get(&val, time.frame)) 
                        return 0;
                
                    index = 0;
                    array = &(val.Get<VtArray<U>>());
                    size = array->size(); // update size to the current frame one
                    if (size == 0)
                        return 0;
                
                    numKeys = 1; // we just want a single key
                    arnoldVec.resize(size);
                    arrayMtx = (VtArray<GfMatrix4d>*)(array);
                    matrices = arrayMtx->data();
                    i = numKeys; // this will stop the "for" loop
                }

                for (size_t v = 0; v < size; ++v, ++index) {
                    AtMatrix& aiMat = arnoldVec[index];
                    const double* matArray = matrices[v].GetArray();
                    for (unsigned int k = 0; k < 4; ++k)
                        for (unsigned int j = 0; j < 4; ++j)
                            aiMat[k][j] = matArray[4 * k + j];
                }
            }
            if (size > 0)
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, numKeys, AI_TYPE_MATRIX, arnoldVec.data()));
        } else {
            A* arnoldVec = new A[size * numKeys], *ptr = arnoldVec;
            for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
                if (i > 0) {
                    // if a time sample is missing, we can't translate 
                    // this attribute properly
                    if (!attr.Get(&val, timeVal)) {
                        size = 0;
                        break;
                    }
                    array = &(val.Get<VtArray<U>>());
                }
                if (array->size() != size) {
                     // Arnold won't support varying element count. 
                    // We need to only consider a single key corresponding to the current frame
                    if (!attr.Get(&val, time.frame)) {
                        size = 0;
                        break;
                    }                        

                    delete [] arnoldVec;
                    array = &(val.Get<VtArray<U>>()); 
                    size = array->size(); // update size to the current frame one
                    numKeys = 1; // we just want a single key now
                    // reallocate the array
                    arnoldVec = new A[size * numKeys];
                    ptr = arnoldVec;
                    i = numKeys; // this will stop the "for" loop after the concatenation
                    
                }
                for (unsigned j=0; j < array->size(); j++)
                    *ptr++ = array->data()[j];
            }

            if (size > 0)
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, numKeys, attrType, arnoldVec));
            else
                numKeys = 0;

            delete [] arnoldVec;
        }
        return numKeys;
    }
}

