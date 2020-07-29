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
// Modifications Copyright 2019 Autodesk, Inc.
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

#include <pxr/base/gf/vec2f.h>

#include <pxr/base/tf/stringUtils.h>

#include <pxr/usd/sdf/assetPath.h>

#include "pxr/imaging/hd/extComputationUtils.h"

#include "constant_strings.h"
#include "debug_codes.h"
#include "hdarnold.h"

#include <type_traits>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (BOOL)
    (BYTE)
    (INT)
    (UINT)
    (FLOAT)
    (VECTOR2)
    (VECTOR)
    (RGB)
    (RGBA)
    (STRING)
    (constant)
    (uniform)
    (varying)
    (indexed)
    ((constantArray, "constant ARRAY"))
    (displayColor)
    ((arnoldPrefix, "arnold:"))
    ((visibilityPrefix, "visibility:"))
    ((sidednessPrefix, "sidedness:"))
    (camera)
    (shadow)
    (diffuse_transmit)
    (specular_transmit)
    (volume)
    (diffuse_reflect)
    (specular_reflect)
    (subsurface)
);
// clang-format on

namespace {

auto nodeSetByteFromInt = [](AtNode* node, const AtString paramName, int v) {
    AiNodeSetByte(node, paramName, static_cast<uint8_t>(v));
};
auto nodeSetByteFromUChar = [](AtNode* node, const AtString paramName, unsigned char v) {
    AiNodeSetByte(node, paramName, static_cast<uint8_t>(v));
};
auto nodeSetByteFromLong = [](AtNode* node, const AtString paramName, long v) {
    AiNodeSetByte(node, paramName, static_cast<uint8_t>(v));
};
auto nodeSetIntFromLong = [](AtNode* node, const AtString paramName, long v) {
    AiNodeSetInt(node, paramName, static_cast<int>(v));
};
auto nodeSetStrFromToken = [](AtNode* node, const AtString paramName, TfToken v) {
    AiNodeSetStr(node, paramName, v.GetText());
};
auto nodeSetStrFromStdStr = [](AtNode* node, const AtString paramName, const std::string& v) {
    AiNodeSetStr(node, paramName, v.c_str());
};
auto nodeSetBoolFromInt = [](AtNode* node, const AtString paramName, int v) { AiNodeSetBool(node, paramName, v != 0); };
auto nodeSetBoolFromLong = [](AtNode* node, const AtString paramName, long v) {
    AiNodeSetBool(node, paramName, v != 0);
};
auto nodeSetFltFromHalf = [](AtNode* node, const AtString paramName, GfHalf v) {
    AiNodeSetFlt(node, paramName, static_cast<float>(v));
};
auto nodeSetFltFromDouble = [](AtNode* node, const AtString paramName, double v) {
    AiNodeSetFlt(node, paramName, static_cast<float>(v));
};
auto nodeSetRGBFromVec3 = [](AtNode* node, const AtString paramName, const GfVec3f& v) {
    AiNodeSetRGB(node, paramName, v[0], v[1], v[2]);
};
auto nodeSetRGBAFromVec4 = [](AtNode* node, const AtString paramName, const GfVec4f& v) {
    AiNodeSetRGBA(node, paramName, v[0], v[1], v[2], v[3]);
};
auto nodeSetVecFromVec3 = [](AtNode* node, const AtString paramName, const GfVec3f& v) {
    AiNodeSetVec(node, paramName, v[0], v[1], v[2]);
};
auto nodeSetVec2FromVec2 = [](AtNode* node, const AtString paramName, const GfVec2f& v) {
    AiNodeSetVec2(node, paramName, v[0], v[1]);
};
auto nodeSetStrFromAssetPath = [](AtNode* node, const AtString paramName, const SdfAssetPath& v) {
    AiNodeSetStr(node, paramName, v.GetResolvedPath().empty() ? v.GetAssetPath().c_str() : v.GetResolvedPath().c_str());
};

const std::array<HdInterpolation, HdInterpolationCount> interpolations{
    HdInterpolationConstant, HdInterpolationUniform,     HdInterpolationVarying,
    HdInterpolationVertex,   HdInterpolationFaceVarying, HdInterpolationInstance,
};

inline bool _Declare(AtNode* node, const TfToken& name, const TfToken& scope, const TfToken& type)
{
    if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), name.GetText()) != nullptr) {
        TF_DEBUG(HDARNOLD_PRIMVARS)
            .Msg(
                "Unable to translate %s primvar for %s due to a name collision with a built-in parameter",
                name.GetText(), AiNodeGetName(node));
        return false;
    }
    if (AiNodeLookUpUserParameter(node, name.GetText()) != nullptr) {
        AiNodeResetParameter(node, name.GetText());
    }
    return AiNodeDeclare(node, name.GetText(), TfStringPrintf("%s %s", scope.GetText(), type.GetText()).c_str());
}

template <typename T>
inline uint32_t _ConvertArray(AtNode* node, const AtString& name, uint8_t arnoldType, const VtValue& value)
{
    if (value.IsHolding<T>()) {
        const auto& v = value.UncheckedGet<T>();
        AiNodeSetArray(node, name, AiArrayConvert(1, 1, arnoldType, &v));
        return 1;
    } else if (value.IsHolding<VtArray<T>>()) {
        const auto& v = value.UncheckedGet<VtArray<T>>();
        auto* arr = AiArrayConvert(v.size(), 1, arnoldType, v.data());
        AiNodeSetArray(node, name, arr);
        return AiArrayGetNumElements(arr);
    }
    return 0;
}

template <typename T>
AtArray* _ArrayConvert(const VtArray<T>& v, uint8_t arnoldType)
{
    return AiArrayConvert(v.size(), 1, arnoldType, v.data());
}

template <>
AtArray* _ArrayConvert<std::string>(const VtArray<std::string>& v, uint8_t arnoldType)
{
    // TODO(pal): Implement.
    return AiArrayAllocate(0, 1, AI_TYPE_STRING);
}

template <>
AtArray* _ArrayConvert<TfToken>(const VtArray<TfToken>& v, uint8_t arnoldType)
{
    // TODO(pal): Implement.
    return AiArrayAllocate(0, 1, AI_TYPE_STRING);
}

template <>
AtArray* _ArrayConvert<SdfAssetPath>(const VtArray<SdfAssetPath>& v, uint8_t arnoldType)
{
    // TODO(pal): Implement.
    return AiArrayAllocate(0, 1, AI_TYPE_STRING);
}

template <typename T>
AtArray* _ArrayConvertIndexed(const VtArray<T>& v, uint8_t arnoldType, const VtIntArray& indices)
{
    const auto numIndices = indices.size();
    const auto numValues = v.size();
    auto* arr = AiArrayAllocate(numIndices, 1, arnoldType);
    auto* mapped = static_cast<T*>(AiArrayMap(arr));
    for (auto id = decltype(numIndices){0}; id < numIndices; id += 1) {
        const auto index = indices[id];
        if (Ai_likely(index < numValues)) {
            mapped[id] = v[indices[id]];
        } else {
            mapped[id] = {};
        }
    }
    AiArrayUnmap(arr);
    return arr;
}

template <>
AtArray* _ArrayConvertIndexed<std::string>(const VtArray<std::string>& v, uint8_t arnoldType, const VtIntArray& indices)
{
    // TODO(pal): Implement.
    return AiArrayAllocate(0, 1, AI_TYPE_STRING);
}

template <>
AtArray* _ArrayConvertIndexed<TfToken>(const VtArray<TfToken>& v, uint8_t arnoldType, const VtIntArray& indices)
{
    // TODO(pal): Implement.
    return AiArrayAllocate(0, 1, AI_TYPE_STRING);
}

template <>
AtArray* _ArrayConvertIndexed<SdfAssetPath>(const VtArray<SdfAssetPath>& v, uint8_t arnoldType, const VtIntArray& indices)
{
    // TODO(pal): Implement.
    return AiArrayAllocate(0, 1, AI_TYPE_STRING);
}

template <typename T>
inline uint32_t _DeclareAndConvertArray(
    AtNode* node, const TfToken& name, const TfToken& scope, const TfToken& type, uint8_t arnoldType,
    const VtValue& value, bool isConstant, void (*f)(AtNode*, const AtString, T))
{
    // We are removing const and reference from the type. When using std::string or SdfAssetPath, we want
    // to use a function pointer with const& type, because we'll be providing our own lambda to do the conversion, and
    // we don't want to copy complex types. For other cases, Arnold functions are receiving types by their value. We
    // can't use a template to automatically deduct the type of the functions, because the AiNodeSet functions have
    // overrides for both const char* and AtString in their second parameter, so we are forcing the deduction using
    // the function pointer.
    using CT = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
    const auto& v = value.UncheckedGet<VtArray<CT>>();
    if (isConstant && v.size() == 1) {
        if (!_Declare(node, name, _tokens->constant, type)) {
            return 0;
        }
        f(node, AtString{name.GetText()}, v[0]);
        return 1;
    }
    if (!_Declare(node, name, scope, type)) {
        return 0;
    }
    auto* arr = _ArrayConvert<CT>(v, arnoldType);
    AiNodeSetArray(node, name.GetText(), arr);
    return AiArrayGetNumElements(arr);
}

template <typename T>
inline void _DeclareAndConvertInstanceArray(
    AtNode* node, const TfToken& name, const TfToken& type, uint8_t arnoldType,
    const VtValue& value, const VtIntArray& indices, void (*f)(AtNode*, const AtString, T))
{
    // See opening comment of _DeclareAndConvertArray .
    using CT = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
    const auto& v = value.UncheckedGet<VtArray<CT>>();
    if (!_Declare(node, name, _tokens->constantArray, type)) {
        return;
    }
    auto* arr = _ArrayConvertIndexed<CT>(v, arnoldType, indices);
    AiNodeSetArray(node, name.GetText(), arr);
}

// This is useful for uniform, vertex and face-varying. We need to know the size
// to generate the indices for faceVarying data.
inline uint32_t _DeclareAndAssignFromArray(
    AtNode* node, const TfToken& name, const TfToken& scope, const VtValue& value, bool isColor,
    bool isConstant = false)
{
    if (value.IsHolding<VtBoolArray>()) {
        return _DeclareAndConvertArray<bool>(
            node, name, scope, _tokens->BOOL, AI_TYPE_BOOLEAN, value, isConstant, AiNodeSetBool);
    } else if (value.IsHolding<VtUCharArray>()) {
        return _DeclareAndConvertArray<VtUCharArray::value_type>(
            node, name, scope, _tokens->BYTE, AI_TYPE_BYTE, value, isConstant, AiNodeSetByte);
    } else if (value.IsHolding<VtUIntArray>()) {
        return _DeclareAndConvertArray<unsigned int>(
            node, name, scope, _tokens->UINT, AI_TYPE_UINT, value, isConstant, AiNodeSetUInt);
    } else if (value.IsHolding<VtIntArray>()) {
        return _DeclareAndConvertArray<int>(
            node, name, scope, _tokens->INT, AI_TYPE_INT, value, isConstant, AiNodeSetInt);
    } else if (value.IsHolding<VtFloatArray>()) {
        return _DeclareAndConvertArray<float>(
            node, name, scope, _tokens->FLOAT, AI_TYPE_FLOAT, value, isConstant, AiNodeSetFlt);
    } else if (value.IsHolding<VtDoubleArray>()) {
        // TODO
    } else if (value.IsHolding<VtVec2fArray>()) {
        return _DeclareAndConvertArray<const GfVec2f&>(
            node, name, scope, _tokens->VECTOR2, AI_TYPE_VECTOR2, value, isConstant, nodeSetVec2FromVec2);
    } else if (value.IsHolding<VtVec3fArray>()) {
        if (isColor) {
            return _DeclareAndConvertArray<const GfVec3f&>(
                node, name, scope, _tokens->RGB, AI_TYPE_RGB, value, isConstant, nodeSetRGBFromVec3);
        } else {
            return _DeclareAndConvertArray<const GfVec3f&>(
                node, name, scope, _tokens->VECTOR, AI_TYPE_VECTOR, value, isConstant, nodeSetVecFromVec3);
        }
    } else if (value.IsHolding<VtVec4fArray>()) {
        return _DeclareAndConvertArray<const GfVec4f&>(
            node, name, scope, _tokens->RGBA, AI_TYPE_RGBA, value, isConstant, nodeSetRGBAFromVec4);
    } else if (value.IsHolding<VtStringArray>()) {
        return _DeclareAndConvertArray<const std::string&>(
            node, name, scope, _tokens->STRING, AI_TYPE_STRING, value, isConstant, nodeSetStrFromStdStr);
    } else if (value.IsHolding<VtTokenArray>()) {
        return _DeclareAndConvertArray<TfToken>(
            node, name, scope, _tokens->STRING, AI_TYPE_STRING, value, isConstant, nodeSetStrFromToken);
    } else if (value.IsHolding<VtArray<SdfAssetPath>>()) {
        return _DeclareAndConvertArray<const SdfAssetPath&>(
            node, name, scope, _tokens->STRING, AI_TYPE_STRING, value, isConstant, nodeSetStrFromAssetPath);
    }
    return 0;
}

inline void _DeclareAndAssignConstant(AtNode* node, const TfToken& name, const VtValue& value, bool isColor = false)
{
    auto declareConstant = [&node, &name](const TfToken& type) -> bool {
        return _Declare(node, name, _tokens->constant, type);
    };
    if (value.IsHolding<bool>()) {
        if (!declareConstant(_tokens->BOOL)) {
            return;
        }
        AiNodeSetBool(node, name.GetText(), value.UncheckedGet<bool>());
    } else if (value.IsHolding<uint8_t>()) {
        if (!declareConstant(_tokens->BYTE)) {
            return;
        }
        AiNodeSetByte(node, name.GetText(), value.UncheckedGet<uint8_t>());
    } else if (value.IsHolding<unsigned int>()) {
        if (!declareConstant(_tokens->UINT)) {
            return;
        }
        AiNodeSetUInt(node, name.GetText(), value.UncheckedGet<unsigned int>());
    } else if (value.IsHolding<int>()) {
        if (!declareConstant(_tokens->INT)) {
            return;
        }
        AiNodeSetInt(node, name.GetText(), value.UncheckedGet<int>());
    } else if (value.IsHolding<float>()) {
        if (!declareConstant(_tokens->FLOAT)) {
            return;
        }
        AiNodeSetFlt(node, name.GetText(), value.UncheckedGet<float>());
    } else if (value.IsHolding<double>()) {
        if (!declareConstant(_tokens->FLOAT)) {
            return;
        }
        AiNodeSetFlt(node, name.GetText(), static_cast<float>(value.UncheckedGet<double>()));
    } else if (value.IsHolding<GfVec2f>()) {
        if (!declareConstant(_tokens->VECTOR2)) {
            return;
        }
        nodeSetVec2FromVec2(node, AtString{name.GetText()}, value.UncheckedGet<GfVec2f>());
    } else if (value.IsHolding<GfVec3f>()) {
        if (isColor) {
            if (!declareConstant(_tokens->RGB)) {
                return;
            }
            nodeSetRGBFromVec3(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3f>());
        } else {
            if (!declareConstant(_tokens->VECTOR)) {
                return;
            }
            nodeSetVecFromVec3(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3f>());
        }
    } else if (value.IsHolding<GfVec4f>()) {
        if (!declareConstant(_tokens->RGBA)) {
            return;
        }
        nodeSetRGBAFromVec4(node, AtString{name.GetText()}, value.UncheckedGet<GfVec4f>());
    } else if (value.IsHolding<TfToken>()) {
        if (!declareConstant(_tokens->STRING)) {
            return;
        }
        nodeSetStrFromToken(node, AtString{name.GetText()}, value.UncheckedGet<TfToken>());
    } else if (value.IsHolding<std::string>()) {
        if (!declareConstant(_tokens->STRING)) {
            return;
        }
        nodeSetStrFromStdStr(node, AtString{name.GetText()}, value.UncheckedGet<std::string>());
    } else {
        // Display color is a special case, where an array with a single
        // element should be translated to a single, constant RGB.
        if (name == _tokens->displayColor && value.IsHolding<VtVec3fArray>()) {
            const auto& v = value.UncheckedGet<VtVec3fArray>();
            if (v.size() == 1) {
                if (declareConstant(_tokens->RGB)) {
                    AiNodeSetRGB(node, name.GetText(), v[0][0], v[0][1], v[0][2]);
                    return;
                }
            }
        }
        _DeclareAndAssignFromArray(node, name, _tokens->constantArray, value, isColor, true);
    }
}

inline void _DeclareAndAssignInstancePrimvar(
    AtNode* node, const TfToken& name, const VtValue& value, bool isColor, const VtIntArray& indices)
{
    if (value.IsHolding<VtBoolArray>()) {
        _DeclareAndConvertInstanceArray<bool>(
            node, name, _tokens->BOOL, AI_TYPE_BOOLEAN, value, indices, AiNodeSetBool);
    } else if (value.IsHolding<VtUCharArray>()) {
        _DeclareAndConvertInstanceArray<VtUCharArray::value_type>(
            node, name, _tokens->BYTE, AI_TYPE_BYTE, value, indices, AiNodeSetByte);
    } else if (value.IsHolding<VtUIntArray>()) {
        _DeclareAndConvertInstanceArray<unsigned int>(
            node, name, _tokens->UINT, AI_TYPE_UINT, value, indices, AiNodeSetUInt);
    } else if (value.IsHolding<VtIntArray>()) {
        _DeclareAndConvertInstanceArray<int>(
            node, name, _tokens->INT, AI_TYPE_INT, value, indices, AiNodeSetInt);
    } else if (value.IsHolding<VtFloatArray>()) {
        _DeclareAndConvertInstanceArray<float>(
            node, name, _tokens->FLOAT, AI_TYPE_FLOAT, value, indices, AiNodeSetFlt);
    } else if (value.IsHolding<VtDoubleArray>()) {
        // TODO
    } else if (value.IsHolding<VtVec2fArray>()) {
        _DeclareAndConvertInstanceArray<const GfVec2f&>(
            node, name, _tokens->VECTOR2, AI_TYPE_VECTOR2, value, indices, nodeSetVec2FromVec2);
    } else if (value.IsHolding<VtVec3fArray>()) {
        if (isColor) {
            _DeclareAndConvertInstanceArray<const GfVec3f&>(
                node, name, _tokens->RGB, AI_TYPE_RGB, value, indices, nodeSetRGBFromVec3);
        } else {
            _DeclareAndConvertInstanceArray<const GfVec3f&>(
                node, name, _tokens->VECTOR, AI_TYPE_VECTOR, value, indices, nodeSetVecFromVec3);
        }
    } else if (value.IsHolding<VtVec4fArray>()) {
        _DeclareAndConvertInstanceArray<const GfVec4f&>(
            node, name, _tokens->RGBA, AI_TYPE_RGBA, value, indices, nodeSetRGBAFromVec4);
    } else if (value.IsHolding<VtStringArray>()) {
        _DeclareAndConvertInstanceArray<const std::string&>(
            node, name, _tokens->STRING, AI_TYPE_STRING, value, indices, nodeSetStrFromStdStr);
    } else if (value.IsHolding<VtTokenArray>()) {
        _DeclareAndConvertInstanceArray<TfToken>(
            node, name, _tokens->STRING, AI_TYPE_STRING, value, indices, nodeSetStrFromToken);
    } else if (value.IsHolding<VtArray<SdfAssetPath>>()) {
        _DeclareAndConvertInstanceArray<const SdfAssetPath&>(
            node, name, _tokens->STRING, AI_TYPE_STRING, value, indices, nodeSetStrFromAssetPath);
    }
}

inline bool _TokenStartsWithToken(const TfToken& t0, const TfToken& t1)
{
    return strncmp(t0.GetText(), t1.GetText(), t1.size()) == 0;
}

inline bool _CharStartsWithToken(const char* c, const TfToken& t) { return strncmp(c, t.GetText(), t.size()) == 0; }

inline uint8_t _GetRayFlag(uint8_t currentFlag, const char* rayName, const VtValue& value)
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
    if (_CharStartsWithToken(rayName, _tokens->camera)) {
        bitFlag = AI_RAY_CAMERA;
    } else if (_CharStartsWithToken(rayName, _tokens->shadow)) {
        bitFlag = AI_RAY_SHADOW;
    } else if (_CharStartsWithToken(rayName, _tokens->diffuse_transmit)) {
        bitFlag = AI_RAY_DIFFUSE_TRANSMIT;
    } else if (_CharStartsWithToken(rayName, _tokens->specular_transmit)) {
        bitFlag = AI_RAY_SPECULAR_TRANSMIT;
    } else if (_CharStartsWithToken(rayName, _tokens->volume)) {
        bitFlag = AI_RAY_VOLUME;
    } else if (_CharStartsWithToken(rayName, _tokens->diffuse_reflect)) {
        bitFlag = AI_RAY_DIFFUSE_REFLECT;
    } else if (_CharStartsWithToken(rayName, _tokens->specular_reflect)) {
        bitFlag = AI_RAY_SPECULAR_REFLECT;
    } else if (_CharStartsWithToken(rayName, _tokens->subsurface)) {
        bitFlag = AI_RAY_SUBSURFACE;
    }
    return flag ? (currentFlag | bitFlag) : (currentFlag & ~bitFlag);
}

inline void _SetRayFlag(AtNode* node, const AtString& paramName, const char* rayName, const VtValue& value)
{
    AiNodeSetByte(node, paramName, _GetRayFlag(AiNodeGetByte(node, paramName), rayName, value));
}

inline void _HdArnoldInsertPrimvar(
    HdArnoldPrimvarMap& primvars, const TfToken& name, const TfToken& role, HdInterpolation interpolation,
    const VtValue& value)
{
    auto it = primvars.find(name);
    if (it == primvars.end()) {
        primvars.insert({name, {value, role, interpolation}});
    } else {
        it->second.value = value;
        it->second.role = role;
        it->second.interpolation = interpolation;
        it->second.dirtied = true;
    }
}

// We are using function pointers instead of template arguments to deduct the function type, because
// Arnold's AiNodeSetXXX functions have overrides in the form of, void (*) (AtNode*, const char*, T v) and
// void (*) (AtNode*, AtString, T v), so the compiler is unable to deduct which function to use.
// Using function pointers to force deduction is the easiest way, yet lambdas are still inlined.
// This way, we can still use AiNodeSetXXX functions where possible, and we only need to create a handful
// of functions to wrap the more complex type conversions.
template <typename T>
inline bool _SetFromValueOrArray(
    AtNode* node, const AtString& paramName, const VtValue& value, void (*f)(AtNode*, const AtString, T))
{
    using CT = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
    if (value.IsHolding<CT>()) {
        f(node, paramName, value.UncheckedGet<CT>());
    } else if (value.IsHolding<VtArray<CT>>()) {
        const auto& arr = value.UncheckedGet<VtArray<CT>>();
        if (!arr.empty()) {
            f(node, paramName, arr[0]);
        }
    } else {
        return false;
    }
    return true;
}

template <typename T0, typename... T>
inline bool _SetFromValueOrArray(
    AtNode* node, const AtString& paramName, const VtValue& value, void (*f0)(AtNode*, const AtString, T0),
    void (*... fs)(AtNode*, const AtString, T))
{
    return _SetFromValueOrArray<T0>(node, paramName, value, std::forward<decltype(f0)>(f0)) ||
           _SetFromValueOrArray<T...>(node, paramName, value, std::forward<decltype(fs)>(fs)...);
}

} // namespace

AtMatrix HdArnoldConvertMatrix(const GfMatrix4d& in)
{
    AtMatrix out = AI_M4_IDENTITY;
    out.data[0][0] = static_cast<float>(in[0][0]);
    out.data[0][1] = static_cast<float>(in[0][1]);
    out.data[0][2] = static_cast<float>(in[0][2]);
    out.data[0][3] = static_cast<float>(in[0][3]);
    out.data[1][0] = static_cast<float>(in[1][0]);
    out.data[1][1] = static_cast<float>(in[1][1]);
    out.data[1][2] = static_cast<float>(in[1][2]);
    out.data[1][3] = static_cast<float>(in[1][3]);
    out.data[2][0] = static_cast<float>(in[2][0]);
    out.data[2][1] = static_cast<float>(in[2][1]);
    out.data[2][2] = static_cast<float>(in[2][2]);
    out.data[2][3] = static_cast<float>(in[2][3]);
    out.data[3][0] = static_cast<float>(in[3][0]);
    out.data[3][1] = static_cast<float>(in[3][1]);
    out.data[3][2] = static_cast<float>(in[3][2]);
    out.data[3][3] = static_cast<float>(in[3][3]);
    return out;
}

AtMatrix HdArnoldConvertMatrix(const GfMatrix4f& in)
{
    AtMatrix out = AI_M4_IDENTITY;
    out.data[0][0] = in[0][0];
    out.data[0][1] = in[0][1];
    out.data[0][2] = in[0][2];
    out.data[0][3] = in[0][3];
    out.data[1][0] = in[1][0];
    out.data[1][1] = in[1][1];
    out.data[1][2] = in[1][2];
    out.data[1][3] = in[1][3];
    out.data[2][0] = in[2][0];
    out.data[2][1] = in[2][1];
    out.data[2][2] = in[2][2];
    out.data[2][3] = in[2][3];
    out.data[3][0] = in[3][0];
    out.data[3][1] = in[3][1];
    out.data[3][2] = in[3][2];
    out.data[3][3] = in[3][3];
    return out;
}

GfMatrix4f HdArnoldConvertMatrix(const AtMatrix& in)
{
    GfMatrix4f out(1.0f);
    out[0][0] = in.data[0][0];
    out[0][1] = in.data[0][1];
    out[0][2] = in.data[0][2];
    out[0][3] = in.data[0][3];
    out[1][0] = in.data[1][0];
    out[1][1] = in.data[1][1];
    out[1][2] = in.data[1][2];
    out[1][3] = in.data[1][3];
    out[2][0] = in.data[2][0];
    out[2][1] = in.data[2][1];
    out[2][2] = in.data[2][2];
    out[2][3] = in.data[2][3];
    out[3][0] = in.data[3][0];
    out[3][1] = in.data[3][1];
    out[3][2] = in.data[3][2];
    out[3][3] = in.data[3][3];
    return out;
}

void HdArnoldSetTransform(AtNode* node, HdSceneDelegate* delegate, const SdfPath& id)
{
    // For now this is hardcoded to two samples and 0.0 / 1.0 sample times.
    constexpr size_t maxSamples = 2;
    HdTimeSampleArray<GfMatrix4d, maxSamples> xf{};
    delegate->SampleTransform(id, &xf);
    if (Ai_unlikely(xf.count == 0)) {
        AiNodeSetArray(node, str::matrix, AiArray(1, 1, AI_TYPE_MATRIX, AiM4Identity()));
        return;
    }
    AtArray* matrices = AiArrayAllocate(1, xf.count, AI_TYPE_MATRIX);
    for (auto i = decltype(xf.count){0}; i < xf.count; ++i) {
        AiArraySetMtx(matrices, i, HdArnoldConvertMatrix(xf.values[i]));
    }
    AiNodeSetArray(node, str::matrix, matrices);
}

void HdArnoldSetTransform(const std::vector<AtNode*>& nodes, HdSceneDelegate* delegate, const SdfPath& id)
{
    constexpr size_t maxSamples = 3;
    HdTimeSampleArray<GfMatrix4d, maxSamples> xf{};
    delegate->SampleTransform(id, &xf);
    AtArray* matrices = AiArrayAllocate(1, xf.count, AI_TYPE_MATRIX);
    for (auto i = decltype(xf.count){0}; i < xf.count; ++i) {
        AiArraySetMtx(matrices, i, HdArnoldConvertMatrix(xf.values[i]));
    }
    const auto nodeCount = nodes.size();
    if (nodeCount > 0) {
        // You can't set the same array on two different nodes,
        // because it causes a double-free.
        // TODO(pal): we need to check if it's still the case with Arnold 5.
        for (auto i = decltype(nodeCount){1}; i < nodeCount; ++i) {
            AiNodeSetArray(nodes[i], str::matrix, AiArrayCopy(matrices));
        }
        AiNodeSetArray(nodes[0], str::matrix, matrices);
    }
}

void HdArnoldSetParameter(AtNode* node, const AtParamEntry* pentry, const VtValue& value)
{
    const auto paramName = AiParamGetName(pentry);
    const auto paramType = AiParamGetType(pentry);
    if (paramType == AI_TYPE_ARRAY) {
        auto* defaultParam = AiParamGetDefault(pentry);
        if (defaultParam->ARRAY() == nullptr) {
            return;
        }
        const auto arrayType = AiArrayGetType(defaultParam->ARRAY());
        switch (arrayType) {
            // TODO(pal): Add support for missing types.
            //            And convert/test different type conversions.
            case AI_TYPE_INT:
            case AI_TYPE_ENUM:
                _ConvertArray<int>(node, paramName, AI_TYPE_INT, value);
                break;
            case AI_TYPE_UINT:
                _ConvertArray<unsigned int>(node, paramName, arrayType, value);
                break;
            case AI_TYPE_BOOLEAN:
                _ConvertArray<bool>(node, paramName, arrayType, value);
                break;
            case AI_TYPE_FLOAT:
            case AI_TYPE_HALF:
                _ConvertArray<float>(node, paramName, AI_TYPE_FLOAT, value);
                break;
            case AI_TYPE_VECTOR2:
                _ConvertArray<GfVec2f>(node, paramName, arrayType, value);
                break;
            case AI_TYPE_RGB:
            case AI_TYPE_VECTOR:
                _ConvertArray<GfVec3f>(node, paramName, arrayType, value);
                break;
            case AI_TYPE_RGBA:
                _ConvertArray<GfVec4f>(node, paramName, arrayType, value);
                break;
            default:
                AiMsgError("Unsupported array parameter %s.%s", AiNodeGetName(node), AiParamGetName(pentry).c_str());
        }
        return;
    }
    switch (paramType) {
        case AI_TYPE_BYTE:
            _SetFromValueOrArray<int, unsigned char, long>(
                node, paramName, value, nodeSetByteFromInt, nodeSetByteFromUChar, nodeSetByteFromLong);
            break;
        case AI_TYPE_INT:
            _SetFromValueOrArray<int, long>(node, paramName, value, AiNodeSetInt, nodeSetIntFromLong);
            break;
        case AI_TYPE_UINT:
        case AI_TYPE_USHORT:
            _SetFromValueOrArray<unsigned int>(node, paramName, value, AiNodeSetUInt);
            break;
        case AI_TYPE_BOOLEAN:
            _SetFromValueOrArray<bool, int, long>(
                node, paramName, value, AiNodeSetBool, nodeSetBoolFromInt, nodeSetBoolFromLong);
            break;
        case AI_TYPE_FLOAT:
        case AI_TYPE_HALF:
            _SetFromValueOrArray<float, GfHalf, double>(
                node, paramName, value, AiNodeSetFlt, nodeSetFltFromHalf, nodeSetFltFromDouble);
            break;
        case AI_TYPE_RGB:
            _SetFromValueOrArray<const GfVec3f&>(node, paramName, value, nodeSetRGBFromVec3);
            break;
        case AI_TYPE_RGBA:
            _SetFromValueOrArray<const GfVec4f&>(node, paramName, value, nodeSetRGBAFromVec4);
            break;
        case AI_TYPE_VECTOR:
            _SetFromValueOrArray<const GfVec3f&>(node, paramName, value, nodeSetVecFromVec3);
            break;
        case AI_TYPE_VECTOR2:
            _SetFromValueOrArray<const GfVec2f&>(node, paramName, value, nodeSetVec2FromVec2);
            break;
        case AI_TYPE_STRING:
            _SetFromValueOrArray<TfToken, const SdfAssetPath&, const std::string&>(
                node, paramName, value, nodeSetStrFromToken, nodeSetStrFromAssetPath, nodeSetStrFromStdStr);
            break;
        case AI_TYPE_POINTER:
        case AI_TYPE_NODE:
            break; // TODO(pal): Should be in the relationships list.
        case AI_TYPE_MATRIX:
            break; // TODO(pal): Implement!
        case AI_TYPE_ENUM:
            _SetFromValueOrArray<int, long, TfToken, const std::string&>(
                node, paramName, value, AiNodeSetInt, nodeSetIntFromLong, nodeSetStrFromToken, nodeSetStrFromStdStr);
            break;
        case AI_TYPE_CLOSURE:
            break; // Should be in the relationships list.
        default:
            AiMsgError("Unsupported parameter %s.%s", AiNodeGetName(node), AiParamGetName(pentry).c_str());
    }
}

bool ConvertPrimvarToBuiltinParameter(AtNode* node, const TfToken& name, const VtValue& value, uint8_t* visibility)
{
    if (!_TokenStartsWithToken(name, _tokens->arnoldPrefix)) {
        return false;
    }

    const auto* paramName = name.GetText() + _tokens->arnoldPrefix.size();
    // We are checking if it's a visibility flag in form of
    // primvars:arnold:visibility:xyz where xyz is a name of a ray type.
    if (_CharStartsWithToken(paramName, _tokens->visibilityPrefix)) {
        const auto* rayName = paramName + _tokens->visibilityPrefix.size();
        if (visibility == nullptr) {
            _SetRayFlag(node, str::visibility, rayName, value);
        } else {
            *visibility = _GetRayFlag(*visibility, rayName, value);
        }
        return true;
    }
    if (_CharStartsWithToken(paramName, _tokens->sidednessPrefix)) {
        const auto* rayName = paramName + _tokens->sidednessPrefix.size();
        _SetRayFlag(node, str::sidedness, rayName, value);
        return true;
    }
    const auto* nodeEntry = AiNodeGetNodeEntry(node);
    const auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, paramName);
    if (paramEntry != nullptr) {
        HdArnoldSetParameter(node, paramEntry, value);
    }
    return true;
}

void HdArnoldSetConstantPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, uint8_t* visibility)
{
    // Remap primvars:arnold:xyz parameters to xyz parameters on the node.
    if (ConvertPrimvarToBuiltinParameter(node, name, value, visibility)) {
        return;
    }
    const auto isColor = role == HdPrimvarRoleTokens->color;
    if (name == HdPrimvarRoleTokens->color && isColor) {
        if (!_Declare(node, name, _tokens->constant, _tokens->RGBA)) {
            return;
        }

        if (value.IsHolding<GfVec4f>()) {
            const auto& v = value.UncheckedGet<GfVec4f>();
            AiNodeSetRGBA(node, name.GetText(), v[0], v[1], v[2], v[3]);
        } else if (value.IsHolding<VtVec4fArray>()) {
            const auto& arr = value.UncheckedGet<VtVec4fArray>();
            if (arr.empty()) {
                return;
            }
            const auto& v = arr[0];
            AiNodeSetRGBA(node, name.GetText(), v[0], v[1], v[2], v[3]);
        }
    }
    _DeclareAndAssignConstant(node, name, value, isColor);
}

void HdArnoldSetConstantPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc,
    uint8_t* visibility)
{
    HdArnoldSetConstantPrimvar(
        node, primvarDesc.name, primvarDesc.role, delegate->Get(id, primvarDesc.name), visibility);
}

void HdArnoldSetUniformPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value)
{
    _DeclareAndAssignFromArray(node, name, _tokens->uniform, value, role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetUniformPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc)
{
    _DeclareAndAssignFromArray(
        node, primvarDesc.name, _tokens->uniform, delegate->Get(id, primvarDesc.name),
        primvarDesc.role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetVertexPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value)
{
    _DeclareAndAssignFromArray(node, name, _tokens->varying, value, role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetVertexPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc)
{
    _DeclareAndAssignFromArray(
        node, primvarDesc.name, _tokens->varying, delegate->Get(id, primvarDesc.name),
        primvarDesc.role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, const VtIntArray* vertexCounts)
{
    const auto numElements =
        _DeclareAndAssignFromArray(node, name, _tokens->indexed, value, role == HdPrimvarRoleTokens->color);
    if (numElements == 0) {
        return;
    }

    AiNodeSetArray(
        node, TfStringPrintf("%sidxs", name.GetText()).c_str(), HdArnoldGenerateIdxs(numElements, vertexCounts));
}

void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc,
    const VtIntArray* vertexCounts)
{
    HdArnoldSetFaceVaryingPrimvar(node, primvarDesc.name, primvarDesc.role, delegate->Get(id, primvarDesc.name));
}

void HdArnoldSetInstancePrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtIntArray& indices, const VtValue& value)
{
    _DeclareAndAssignInstancePrimvar(node, TfToken{TfStringPrintf("instance_%s", name.GetText())}, value, role == HdPrimvarRoleTokens->color, indices);
}

size_t HdArnoldSetPositionFromPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const AtString& paramName)
{
    HdArnoldSampledPrimvarType xf;
    delegate->SamplePrimvar(id, HdTokens->points, &xf);
    if (xf.count == 0 ||
#ifdef USD_HAS_UPDATED_TIME_SAMPLE_ARRAY
        xf.values.empty() ||
#endif
        ARCH_UNLIKELY(!xf.values[0].IsHolding<VtVec3fArray>())) {
        return 0;
    }
    const auto& v0 = xf.values[0].Get<VtVec3fArray>();
    for (auto index = decltype(xf.count){1}; index < xf.count; index += 1) {
        if (ARCH_UNLIKELY(!xf.values[index].IsHolding<VtVec3fArray>())) {
            xf.count = index;
            break;
        }
    }
    auto* arr = AiArrayAllocate(v0.size(), xf.count, AI_TYPE_VECTOR);
    AiArraySetKey(arr, 0, v0.data());
    for (auto index = decltype(xf.count){1}; index < xf.count; index += 1) {
        const auto& vi = xf.values[index].Get<VtVec3fArray>();
        if (ARCH_LIKELY(vi.size() == v0.size())) {
            AiArraySetKey(arr, 1, vi.data());
        } else {
            AiArraySetKey(arr, 1, v0.data());
        }
    }
    AiNodeSetArray(node, paramName, arr);
    return xf.count;
}

void HdArnoldSetPositionFromValue(AtNode* node, const AtString& paramName, const VtValue& value)
{
    if (!value.IsHolding<VtVec3fArray>()) {
        return;
    }
    const auto& values = value.UncheckedGet<VtVec3fArray>();
    AiNodeSetArray(node, paramName, AiArrayConvert(values.size(), 1, AI_TYPE_VECTOR, values.data()));
}

void HdArnoldSetRadiusFromPrimvar(AtNode* node, const SdfPath& id, HdSceneDelegate* delegate)
{
    HdArnoldSampledPrimvarType xf;
    delegate->SamplePrimvar(id, HdTokens->widths, &xf);
    if (xf.count == 0 ||
#ifdef USD_HAS_UPDATED_TIME_SAMPLE_ARRAY
        xf.values.empty() ||
#endif
        ARCH_UNLIKELY(!xf.values[0].IsHolding<VtFloatArray>())) {
        return;
    }
    const auto& v0 = xf.values[0].Get<VtFloatArray>();
    for (auto index = decltype(xf.count){1}; index < xf.count; index += 1) {
        if (ARCH_UNLIKELY(!xf.values[index].IsHolding<VtFloatArray>())) {
            xf.count = index;
            break;
        }
    }
    auto* arr = AiArrayAllocate(v0.size(), xf.count, AI_TYPE_FLOAT);
    auto* out = static_cast<float*>(AiArrayMapKey(arr, 0));
    auto convertWidth = [](const float w) -> float { return w * 0.5f; };
    std::transform(v0.begin(), v0.end(), out, convertWidth);
    for (auto index = decltype(xf.count){1}; index < xf.count; index += 1) {
        out = static_cast<float*>(AiArrayMapKey(arr, index));
        const auto& vi = xf.values[index].Get<VtFloatArray>();
        if (ARCH_LIKELY(vi.size() == v0.size())) {
            std::transform(vi.begin(), vi.end(), out, convertWidth);
        } else {
            std::transform(v0.begin(), v0.end(), out, convertWidth);
        }
    }
    AiNodeSetArray(node, str::radius, arr);
}

void HdArnoldSetRadiusFromValue(AtNode* node, const VtValue& value)
{
    AtArray* arr = nullptr;
    if (value.IsHolding<VtFloatArray>()) {
        const auto& values = value.UncheckedGet<VtFloatArray>();
        arr = AiArrayAllocate(values.size(), 1, AI_TYPE_FLOAT);
        auto* out = static_cast<float*>(AiArrayMap(arr));
        std::transform(values.begin(), values.end(), out, [](const float w) -> float { return w * 0.5f; });
        AiArrayUnmap(arr);
    } else if (value.IsHolding<VtDoubleArray>()) {
        const auto& values = value.UncheckedGet<VtDoubleArray>();
        arr = AiArrayAllocate(values.size(), 1, AI_TYPE_FLOAT);
        auto* out = static_cast<float*>(AiArrayMap(arr));
        std::transform(values.begin(), values.end(), out, [](const double w) -> float { return static_cast<float>(w * 0.5); });
        AiArrayUnmap(arr);
    } else if (value.IsHolding<float>()) {
        arr = AiArray(1, 1, AI_TYPE_FLOAT, value.UncheckedGet<float>() / 2.0f);
    } else if (value.IsHolding<double>()) {
        arr = AiArray(1, 1, AI_TYPE_FLOAT, static_cast<float>(value.UncheckedGet<double>() / 2.0));
    } else {
        return;
    }

    AiNodeSetArray(node, str::radius, arr);
}

AtArray* HdArnoldGenerateIdxs(unsigned int numIdxs, const VtIntArray* vertexCounts)
{
    auto* array = AiArrayAllocate(numIdxs, 1, AI_TYPE_UINT);
    auto* out = static_cast<uint32_t*>(AiArrayMap(array));
    // Flip indices per polygon to support left handed topologies.
    if (vertexCounts != nullptr && !vertexCounts->empty()) {
        const auto numFaces = vertexCounts->size();
        unsigned int vertexId = 0;
        for (auto vertexCount : *vertexCounts) {
            if (Ai_unlikely(vertexCount <= 0)) {
                continue;
            }
            for (auto vertex = decltype(vertexCount){0}; vertex < vertexCount; vertex += 1) {
                out[vertexId + vertex] = vertexId + vertexCount - vertex - 1;
            }
            vertexId += vertexCount;
        }
    } else {
        for (auto index = decltype(numIdxs){0}; index < numIdxs; index += 1) {
            out[index] = index;
        }
    }
    AiArrayUnmap(array);
    return array;
}

bool HdArnoldGetComputedPrimvars(
    HdSceneDelegate* delegate, const SdfPath& id, HdDirtyBits dirtyBits, HdArnoldPrimvarMap& primvars)
{
    // First we are querying which primvars need to be computed, and storing them in a list to rely on
    // the batched computation function in HdExtComputationUtils.
    HdExtComputationPrimvarDescriptorVector dirtyPrimvars;
    for (auto interpolation : interpolations) {
        const auto computedPrimvars = delegate->GetExtComputationPrimvarDescriptors(id, interpolation);
        for (const auto& primvar : computedPrimvars) {
            if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, primvar.name)) {
                dirtyPrimvars.emplace_back(primvar);
            }
        }
    }

    // Early exit.
    if (dirtyPrimvars.empty()) {
        return false;
    }

    auto changed = false;
    auto valueStore = HdExtComputationUtils::GetComputedPrimvarValues(dirtyPrimvars, delegate);
    for (const auto& primvar : dirtyPrimvars) {
        const auto itComputed = valueStore.find(primvar.name);
        if (itComputed == valueStore.end()) {
            continue;
        }
        changed = true;
        _HdArnoldInsertPrimvar(primvars, primvar.name, primvar.role, primvar.interpolation, itComputed->second);
    }

    return changed;
}

void HdArnoldGetPrimvars(
    HdSceneDelegate* delegate, const SdfPath& id, HdDirtyBits dirtyBits, bool multiplePositionKeys,
    HdArnoldPrimvarMap& primvars)
{
    for (auto interpolation : interpolations) {
        const auto primvarDescs = delegate->GetPrimvarDescriptors(id, interpolation);
        for (const auto& primvarDesc : primvarDescs) {
            if (primvarDesc.name == HdTokens->points) {
                continue;
            }
            // The number of motion keys has to be matched between points and normals, so
            _HdArnoldInsertPrimvar(
                primvars, primvarDesc.name, primvarDesc.role, primvarDesc.interpolation,
                (multiplePositionKeys && primvarDesc.name == HdTokens->normals) ? VtValue{}
                                                                                : delegate->Get(id, primvarDesc.name));
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
