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

#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2h.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4h.h>

#include <pxr/base/tf/stringUtils.h>

#include <pxr/usd/sdf/assetPath.h>

#include "pxr/imaging/hd/extComputationUtils.h"

#include <constant_strings.h>
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
    ((autobumpVisibilityPrefix, "autobump_visibility:"))
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
    AiNodeSetStr(node, paramName, AtString(v.GetText()));
};
auto nodeSetStrFromStdStr = [](AtNode* node, const AtString paramName, const std::string& v) {
    AiNodeSetStr(node, paramName, AtString(v.c_str()));
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
auto nodeSetRGBFromVec3h = [](AtNode* node, const AtString paramName, const GfVec3h& v) {
    AiNodeSetRGB(node, paramName, static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
};
auto nodeSetRGBAFromVec4h = [](AtNode* node, const AtString paramName, const GfVec4h& v) {
    AiNodeSetRGBA(
        node, paramName, static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]),
        static_cast<float>(v[3]));
};
auto nodeSetVecFromVec3h = [](AtNode* node, const AtString paramName, const GfVec3h& v) {
    AiNodeSetVec(node, paramName, static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
};
auto nodeSetVec2FromVec2h = [](AtNode* node, const AtString paramName, const GfVec2h& v) {
    AiNodeSetVec2(node, paramName, static_cast<float>(v[0]), static_cast<float>(v[1]));
};
auto nodeSetRGBFromVec3d = [](AtNode* node, const AtString paramName, const GfVec3d& v) {
    AiNodeSetRGB(node, paramName, static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
};
auto nodeSetRGBAFromVec4d = [](AtNode* node, const AtString paramName, const GfVec4d& v) {
    AiNodeSetRGBA(
        node, paramName, static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]),
        static_cast<float>(v[3]));
};
auto nodeSetVecFromVec3d = [](AtNode* node, const AtString paramName, const GfVec3d& v) {
    AiNodeSetVec(node, paramName, static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
};
auto nodeSetVec2FromVec2d = [](AtNode* node, const AtString paramName, const GfVec2d& v) {
    AiNodeSetVec2(node, paramName, static_cast<float>(v[0]), static_cast<float>(v[1]));
};

auto nodeSetMatrixFromMatrix4f = [](AtNode* node, const AtString paramName, const GfMatrix4f& m) {
    AtMatrix atMatrix;
    std::copy_n(m.data(), 16, &atMatrix.data[0][0]);
    AiNodeSetMatrix(node, paramName, atMatrix);
};

auto nodeSetMatrixFromMatrix4d = [](AtNode* node, const AtString paramName, const GfMatrix4d& m) {
    AiNodeSetMatrix(node, paramName, HdArnoldConvertMatrix(m));
};

auto nodeSetStrFromAssetPath = [](AtNode* node, const AtString paramName, const SdfAssetPath& v) {
    AiNodeSetStr(
        node, paramName,
        v.GetResolvedPath().empty() ? AtString(v.GetAssetPath().c_str()) : AtString(v.GetResolvedPath().c_str()));
};

const std::vector<HdInterpolation> primvarInterpolations{
    HdInterpolationConstant, HdInterpolationUniform,     HdInterpolationVarying,
    HdInterpolationVertex,   HdInterpolationFaceVarying, HdInterpolationInstance,
};

inline bool _Declare(AtNode* node, const TfToken& name, const TfToken& scope, const TfToken& type)
{
    if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), AtString(name.GetText())) != nullptr) {
        TF_DEBUG(HDARNOLD_PRIMVARS)
            .Msg(
                "Unable to translate %s primvar for %s due to a name collision with a built-in parameter",
                name.GetText(), AiNodeGetName(node));
        return false;
    }
    if (AiNodeLookUpUserParameter(node, AtString(name.GetText())) != nullptr) {
        AiNodeResetParameter(node, AtString(name.GetText()));
    }
    return AiNodeDeclare(
        node, AtString(name.GetText()), AtString(TfStringPrintf("%s %s", scope.GetText(), type.GetText()).c_str()));
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

template <typename TO, typename FROM>
inline uint32_t _ConvertArrayTyped(AtNode* node, const AtString& name, uint8_t arnoldType, const VtValue& value)
{
    if (value.IsHolding<FROM>()) {
        const auto& v = value.UncheckedGet<FROM>();
        AiNodeSetArray(node, name, AiArray(1, 1, arnoldType, static_cast<TO>(v)));
        return 1;
    } else if (value.IsHolding<VtArray<FROM>>()) {
        const auto& v = value.UncheckedGet<VtArray<FROM>>();
        auto* arr = AiArrayAllocate(v.size(), 1, arnoldType);
        std::transform(v.begin(), v.end(), reinterpret_cast<TO*>(AiArrayMap(arr)), [](const FROM& from) -> TO {
            return static_cast<TO>(from);
        });
        AiArrayUnmap(arr);
        AiNodeSetArray(node, name, arr);
        return AiArrayGetNumElements(arr);
    }
    return 0;
}

template <typename TO, typename FROM>
inline uint32_t _ConvertArrayTuple(AtNode* node, const AtString& name, uint8_t arnoldType, const VtValue& value)
{
    if (value.IsHolding<FROM>()) {
        const auto& v = value.UncheckedGet<FROM>();
        auto* arr = AiArrayAllocate(1, 1, arnoldType);
        *reinterpret_cast<TO*>(AiArrayMap(arr)) = TO{v};
        AiArrayUnmap(arr);
        AiNodeSetArray(node, name, arr);
        return 1;
    } else if (value.IsHolding<VtArray<FROM>>()) {
        const auto& v = value.UncheckedGet<VtArray<FROM>>();
        auto* arr = AiArrayAllocate(v.size(), 1, arnoldType);
        std::transform(v.begin(), v.end(), reinterpret_cast<TO*>(AiArrayMap(arr)), [](const FROM& from) -> TO {
            return TO{from};
        });
        AiArrayUnmap(arr);
        AiNodeSetArray(node, name, arr);
        return AiArrayGetNumElements(arr);
    }
    return 0;
}

// AtString requires conversion and can't be trivially copied.
template <typename T>
inline void _ConvertToString(AtString& to, const T& from)
{
    to = {};
}

template <>
inline void _ConvertToString<std::string>(AtString& to, const std::string& from)
{
    to = AtString{from.c_str()};
}

template <>
inline void _ConvertToString<TfToken>(AtString& to, const TfToken& from)
{
    to = AtString{from.GetText()};
}

template <>
inline void _ConvertToString<SdfAssetPath>(AtString& to, const SdfAssetPath& from)
{
    to = AtString{from.GetResolvedPath().empty() ? from.GetAssetPath().c_str() : from.GetResolvedPath().c_str()};
}

// Anything but string should be trivially copyable.
template <typename T>
AtArray* _ArrayConvert(const VtArray<T>& v, uint8_t arnoldType)
{
    if (arnoldType == AI_TYPE_STRING) {
        auto* arr = AiArrayAllocate(v.size(), 1, AI_TYPE_STRING);
        auto* mapped = static_cast<AtString*>(AiArrayMap(arr));
        for (const auto& from : v) {
            _ConvertToString(*mapped, from);
            mapped += 1;
        }
        AiArrayUnmap(arr);
        return arr;
    } else {
        return AiArrayConvert(v.size(), 1, arnoldType, v.data());
    }
}

template <typename T>
AtArray* _ArrayConvertIndexed(const VtArray<T>& v, uint8_t arnoldType, const VtIntArray& indices)
{
    const auto numIndices = indices.size();
    const auto numValues = v.size();
    auto* arr = AiArrayAllocate(numIndices, 1, arnoldType);
    if (arnoldType == AI_TYPE_STRING) {
        auto* mapped = static_cast<AtString*>(AiArrayMap(arr));
        for (auto id = decltype(numIndices){0}; id < numIndices; id += 1) {
            const auto index = indices[id];
            if (Ai_likely(index >= 0 && static_cast<size_t>(index) < numValues)) {
                _ConvertToString(mapped[id], v[index]);
            } else {
                mapped[id] = {};
            }
        }
    } else {
        auto* mapped = static_cast<T*>(AiArrayMap(arr));
        for (auto id = decltype(numIndices){0}; id < numIndices; id += 1) {
            const auto index = indices[id];
            if (Ai_likely(index >= 0 && static_cast<size_t>(index) < numValues)) {
                mapped[id] = v[index];
            } else {
                mapped[id] = {};
            }
        }
    }

    AiArrayUnmap(arr);
    return arr;
}

template <typename TO, typename FROM>
inline uint32_t _DeclareAndConvertArrayTyped(
    AtNode* node, const TfToken& name, const TfToken& scope, const TfToken& type, uint8_t arnoldType,
    const VtValue& value, bool isConstant, void (*f)(AtNode*, const AtString, FROM))
{
    using CFROM = typename std::remove_cv<typename std::remove_reference<FROM>::type>::type;
    const auto& v = value.UncheckedGet<VtArray<CFROM>>();
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
    auto* arr = AiArrayAllocate(v.size(), 1, arnoldType);
    std::transform(v.begin(), v.end(), reinterpret_cast<TO*>(AiArrayMap(arr)), [](const FROM& from) -> TO {
        return static_cast<TO>(from);
    });
    AiArrayUnmap(arr);
    AiNodeSetArray(node, AtString(name.GetText()), arr);
    return AiArrayGetNumElements(arr);
}

template <typename TO, typename FROM>
inline uint32_t _DeclareAndConvertArrayTuple(
    AtNode* node, const TfToken& name, const TfToken& scope, const TfToken& type, uint8_t arnoldType,
    const VtValue& value, bool isConstant, void (*f)(AtNode*, const AtString, FROM))
{
    using CFROM = typename std::remove_cv<typename std::remove_reference<FROM>::type>::type;
    const auto& v = value.UncheckedGet<VtArray<CFROM>>();
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
    auto* arr = AiArrayAllocate(v.size(), 1, arnoldType);
    std::transform(
        reinterpret_cast<const typename CFROM::ScalarType*>(v.data()),
        reinterpret_cast<const typename CFROM::ScalarType*>(v.data()) + v.size() * CFROM::dimension,
        reinterpret_cast<TO*>(AiArrayMap(arr)),
        [](const typename CFROM::ScalarType& from) -> TO { return static_cast<TO>(from); });
    AiArrayUnmap(arr);
    AiNodeSetArray(node, AtString(name.GetText()), arr);
    return AiArrayGetNumElements(arr);
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
    AiNodeSetArray(node, AtString(name.GetText()), arr);
    return AiArrayGetNumElements(arr);
}

template <typename TO, typename FROM>
inline void _DeclareAndConvertInstanceArrayTyped(
    AtNode* node, const TfToken& name, const TfToken& type, uint8_t arnoldType, const VtValue& value,
    const VtIntArray& indices)
{
    if (indices.empty()) {
        return;
    }
    const auto numIndices = indices.size();
    // See opening comment of _DeclareAndConvertArray .
    using CFROM = typename std::remove_cv<typename std::remove_reference<FROM>::type>::type;
    const auto& v = value.UncheckedGet<VtArray<CFROM>>();
    if (v.empty()) {
        return;
    }
    const auto numValues = v.size();
    if (!_Declare(node, name, _tokens->constantArray, type)) {
        return;
    }
    auto* arr = AiArrayAllocate(numIndices, 1, arnoldType);
    auto* mapped = reinterpret_cast<TO*>(AiArrayMap(arr));
    for (auto id = decltype(numIndices){0}; id < numIndices; id += 1) {
        const auto index = indices[id];
        if (Ai_likely(index >= 0 && static_cast<size_t>(index) < numValues)) {
            mapped[id] = static_cast<TO>(v[index]);
        } else {
            mapped[id] = {};
        }
    }
    AiArrayUnmap(arr);
    AiNodeSetArray(node, AtString(name.GetText()), arr);
}

template <typename TO, typename FROM>
inline void _DeclareAndConvertInstanceArrayTuple(
    AtNode* node, const TfToken& name, const TfToken& type, uint8_t arnoldType, const VtValue& value,
    const VtIntArray& indices)
{
    if (indices.empty()) {
        return;
    }
    const auto numIndices = indices.size();
    // See opening comment of _DeclareAndConvertArray .
    using CFROM = typename std::remove_cv<typename std::remove_reference<FROM>::type>::type;
    const auto& v = value.UncheckedGet<VtArray<CFROM>>();
    if (v.empty()) {
        return;
    }
    const auto numValues = v.size();
    if (!_Declare(node, name, _tokens->constantArray, type)) {
        return;
    }
    auto* arr = AiArrayAllocate(numIndices, 1, arnoldType);
    auto* mapped = reinterpret_cast<TO*>(AiArrayMap(arr));
    auto* data = reinterpret_cast<const typename CFROM::ScalarType*>(v.data());
    for (auto id = decltype(numIndices){0}; id < numIndices; id += 1) {
        const auto index = indices[id];
        if (Ai_likely(index >= 0 && static_cast<size_t>(index) < numValues)) {
            std::transform(
                data + index * CFROM::dimension, data + (index + 1) * CFROM::dimension, mapped + id * CFROM::dimension,
                [](const typename CFROM::ScalarType& from) -> TO { return static_cast<TO>(from); });
        } else {
            std::fill(mapped + id * CFROM::dimension, mapped + (id + 1) * CFROM::dimension, TO{0});
        }
    }
    AiArrayUnmap(arr);
    AiNodeSetArray(node, AtString(name.GetText()), arr);
}

template <typename T>
inline void _DeclareAndConvertInstanceArray(
    AtNode* node, const TfToken& name, const TfToken& type, uint8_t arnoldType, const VtValue& value,
    const VtIntArray& indices)
{
    // See opening comment of _DeclareAndConvertArray .
    using CT = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
    const auto& v = value.UncheckedGet<VtArray<CT>>();
    if (!_Declare(node, name, _tokens->constantArray, type)) {
        return;
    }
    auto* arr = _ArrayConvertIndexed<CT>(v, arnoldType, indices);
    AiNodeSetArray(node, AtString(name.GetText()), arr);
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
    } else if (value.IsHolding<VtArray<GfHalf>>()) { // HALF types
        return _DeclareAndConvertArrayTyped<float, GfHalf>(
            node, name, scope, _tokens->FLOAT, AI_TYPE_FLOAT, value, isConstant, nodeSetFltFromHalf);
    } else if (value.IsHolding<VtArray<GfVec2h>>()) {
        return _DeclareAndConvertArrayTuple<float, const GfVec2h&>(
            node, name, scope, _tokens->VECTOR2, AI_TYPE_VECTOR2, value, isConstant, nodeSetVec2FromVec2h);
    } else if (value.IsHolding<VtArray<GfVec3h>>()) {
        if (isColor) {
            return _DeclareAndConvertArrayTuple<float, const GfVec3h&>(
                node, name, scope, _tokens->RGB, AI_TYPE_RGB, value, isConstant, nodeSetRGBFromVec3h);
        } else {
            return _DeclareAndConvertArrayTuple<float, const GfVec3h&>(
                node, name, scope, _tokens->VECTOR, AI_TYPE_VECTOR, value, isConstant, nodeSetRGBFromVec3h);
        }
    } else if (value.IsHolding<VtArray<GfVec4h>>()) {
        return _DeclareAndConvertArrayTuple<float, const GfVec4h&>(
            node, name, scope, _tokens->RGBA, AI_TYPE_RGBA, value, isConstant, nodeSetRGBAFromVec4h);
    } else if (value.IsHolding<VtArray<double>>()) { // double types
        return _DeclareAndConvertArrayTyped<float, double>(
            node, name, scope, _tokens->FLOAT, AI_TYPE_FLOAT, value, isConstant, nodeSetFltFromDouble);
    } else if (value.IsHolding<VtArray<GfVec2d>>()) {
        return _DeclareAndConvertArrayTuple<float, const GfVec2d&>(
            node, name, scope, _tokens->VECTOR2, AI_TYPE_VECTOR2, value, isConstant, nodeSetVec2FromVec2d);
    } else if (value.IsHolding<VtArray<GfVec3d>>()) {
        if (isColor) {
            return _DeclareAndConvertArrayTuple<float, const GfVec3d&>(
                node, name, scope, _tokens->RGB, AI_TYPE_RGB, value, isConstant, nodeSetRGBFromVec3d);
        } else {
            return _DeclareAndConvertArrayTuple<float, const GfVec3d&>(
                node, name, scope, _tokens->VECTOR, AI_TYPE_VECTOR, value, isConstant, nodeSetRGBFromVec3d);
        }
    } else if (value.IsHolding<VtArray<GfVec4d>>()) {
        return _DeclareAndConvertArrayTuple<float, const GfVec4d&>(
            node, name, scope, _tokens->RGBA, AI_TYPE_RGBA, value, isConstant, nodeSetRGBAFromVec4d);
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
        AiNodeSetBool(node, AtString(name.GetText()), value.UncheckedGet<bool>());
    } else if (value.IsHolding<uint8_t>()) {
        if (!declareConstant(_tokens->BYTE)) {
            return;
        }
        AiNodeSetByte(node, AtString(name.GetText()), value.UncheckedGet<uint8_t>());
    } else if (value.IsHolding<unsigned int>()) {
        if (!declareConstant(_tokens->UINT)) {
            return;
        }
        AiNodeSetUInt(node, AtString(name.GetText()), value.UncheckedGet<unsigned int>());
    } else if (value.IsHolding<int>()) {
        if (!declareConstant(_tokens->INT)) {
            return;
        }
        AiNodeSetInt(node, AtString(name.GetText()), value.UncheckedGet<int>());
    } else if (value.IsHolding<float>()) {
        if (!declareConstant(_tokens->FLOAT)) {
            return;
        }
        AiNodeSetFlt(node, AtString(name.GetText()), value.UncheckedGet<float>());
    } else if (value.IsHolding<double>()) {
        if (!declareConstant(_tokens->FLOAT)) {
            return;
        }
        AiNodeSetFlt(node, AtString(name.GetText()), static_cast<float>(value.UncheckedGet<double>()));
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
    } else if (value.IsHolding<GfHalf>()) {
        if (!declareConstant(_tokens->FLOAT)) {
            return;
        }
        nodeSetFltFromHalf(node, AtString{name.GetText()}, value.UncheckedGet<GfHalf>());
    } else if (value.IsHolding<GfVec2h>()) {
        if (!declareConstant(_tokens->VECTOR2)) {
            return;
        }
        nodeSetVec2FromVec2h(node, AtString{name.GetText()}, value.UncheckedGet<GfVec2h>());
    } else if (value.IsHolding<GfVec3h>()) {
        if (isColor) {
            if (!declareConstant(_tokens->RGB)) {
                return;
            }
            nodeSetRGBFromVec3h(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3h>());
        } else {
            if (!declareConstant(_tokens->VECTOR)) {
                return;
            }
            nodeSetVecFromVec3h(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3h>());
        }
    } else if (value.IsHolding<GfVec4h>()) {
        if (!declareConstant(_tokens->RGBA)) {
            return;
        }
        nodeSetRGBAFromVec4h(node, AtString{name.GetText()}, value.UncheckedGet<GfVec4h>());
    } else if (value.IsHolding<double>()) {
        if (!declareConstant(_tokens->FLOAT)) {
            return;
        }
        nodeSetFltFromDouble(node, AtString{name.GetText()}, value.UncheckedGet<double>());
    } else if (value.IsHolding<GfVec2d>()) {
        if (!declareConstant(_tokens->VECTOR2)) {
            return;
        }
        nodeSetVec2FromVec2d(node, AtString{name.GetText()}, value.UncheckedGet<GfVec2d>());
    } else if (value.IsHolding<GfVec3d>()) {
        if (isColor) {
            if (!declareConstant(_tokens->RGB)) {
                return;
            }
            nodeSetRGBFromVec3d(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3d>());
        } else {
            if (!declareConstant(_tokens->VECTOR)) {
                return;
            }
            nodeSetVecFromVec3d(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3d>());
        }
    } else if (value.IsHolding<GfVec4d>()) {
        if (!declareConstant(_tokens->RGBA)) {
            return;
        }
        nodeSetRGBAFromVec4d(node, AtString{name.GetText()}, value.UncheckedGet<GfVec4d>());
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
    } else if (value.IsHolding<SdfAssetPath>()) {
        if (!declareConstant(_tokens->STRING)) {
            return;
        }
        nodeSetStrFromAssetPath(node, AtString{name.GetText()}, value.UncheckedGet<SdfAssetPath>());
    } else {
        // Display color is a special case, where an array with a single
        // element should be translated to a single, constant RGB.
        if (name == _tokens->displayColor && value.IsHolding<VtVec3fArray>()) {
            const auto& v = value.UncheckedGet<VtVec3fArray>();
            if (v.size() == 1) {
                if (declareConstant(_tokens->RGB)) {
                    AiNodeSetRGB(node, AtString(name.GetText()), v[0][0], v[0][1], v[0][2]);
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
        _DeclareAndConvertInstanceArray<bool>(node, name, _tokens->BOOL, AI_TYPE_BOOLEAN, value, indices);
    } else if (value.IsHolding<VtUCharArray>()) {
        _DeclareAndConvertInstanceArray<VtUCharArray::value_type>(
            node, name, _tokens->BYTE, AI_TYPE_BYTE, value, indices);
    } else if (value.IsHolding<VtUIntArray>()) {
        _DeclareAndConvertInstanceArray<unsigned int>(node, name, _tokens->UINT, AI_TYPE_UINT, value, indices);
    } else if (value.IsHolding<VtIntArray>()) {
        _DeclareAndConvertInstanceArray<int>(node, name, _tokens->INT, AI_TYPE_INT, value, indices);
    } else if (value.IsHolding<VtFloatArray>()) {
        _DeclareAndConvertInstanceArray<float>(node, name, _tokens->FLOAT, AI_TYPE_FLOAT, value, indices);
    } else if (value.IsHolding<VtVec2fArray>()) {
        _DeclareAndConvertInstanceArray<const GfVec2f&>(node, name, _tokens->VECTOR2, AI_TYPE_VECTOR2, value, indices);
    } else if (value.IsHolding<VtVec3fArray>()) {
        if (isColor) {
            _DeclareAndConvertInstanceArray<const GfVec3f&>(node, name, _tokens->RGB, AI_TYPE_RGB, value, indices);
        } else {
            _DeclareAndConvertInstanceArray<const GfVec3f&>(
                node, name, _tokens->VECTOR, AI_TYPE_VECTOR, value, indices);
        }
    } else if (value.IsHolding<VtVec4fArray>()) {
        _DeclareAndConvertInstanceArray<const GfVec4f&>(node, name, _tokens->RGBA, AI_TYPE_RGBA, value, indices);
    } else if (value.IsHolding<VtStringArray>()) {
        _DeclareAndConvertInstanceArray<const std::string&>(
            node, name, _tokens->STRING, AI_TYPE_STRING, value, indices);
    } else if (value.IsHolding<VtTokenArray>()) {
        _DeclareAndConvertInstanceArray<TfToken>(node, name, _tokens->STRING, AI_TYPE_STRING, value, indices);
    } else if (value.IsHolding<VtArray<SdfAssetPath>>()) {
        _DeclareAndConvertInstanceArray<const SdfAssetPath&>(
            node, name, _tokens->STRING, AI_TYPE_STRING, value, indices);
    } else if (value.IsHolding<VtArray<GfHalf>>()) { // Half types
        _DeclareAndConvertInstanceArrayTyped<float, GfHalf>(node, name, _tokens->FLOAT, AI_TYPE_FLOAT, value, indices);
    } else if (value.IsHolding<VtArray<GfVec2h>>()) {
        _DeclareAndConvertInstanceArrayTuple<float, GfVec2h>(
            node, name, _tokens->VECTOR2, AI_TYPE_VECTOR2, value, indices);
    } else if (value.IsHolding<VtArray<GfVec3h>>()) {
        if (isColor) {
            _DeclareAndConvertInstanceArrayTuple<float, GfVec3h>(node, name, _tokens->RGB, AI_TYPE_RGB, value, indices);
        } else {
            _DeclareAndConvertInstanceArrayTuple<float, GfVec3h>(
                node, name, _tokens->VECTOR, AI_TYPE_VECTOR, value, indices);
        }
    } else if (value.IsHolding<VtArray<GfVec4h>>()) {
        _DeclareAndConvertInstanceArrayTuple<float, GfVec4h>(node, name, _tokens->RGBA, AI_TYPE_RGBA, value, indices);
    } else if (value.IsHolding<VtArray<double>>()) { // double types
        _DeclareAndConvertInstanceArrayTyped<float, double>(node, name, _tokens->FLOAT, AI_TYPE_FLOAT, value, indices);
    } else if (value.IsHolding<VtArray<GfVec2d>>()) {
        _DeclareAndConvertInstanceArrayTuple<float, GfVec2d>(
            node, name, _tokens->VECTOR2, AI_TYPE_VECTOR2, value, indices);
    } else if (value.IsHolding<VtArray<GfVec3d>>()) {
        if (isColor) {
            _DeclareAndConvertInstanceArrayTuple<float, GfVec3d>(node, name, _tokens->RGB, AI_TYPE_RGB, value, indices);
        } else {
            _DeclareAndConvertInstanceArrayTuple<float, GfVec3d>(
                node, name, _tokens->VECTOR, AI_TYPE_VECTOR, value, indices);
        }
    } else if (value.IsHolding<VtArray<GfVec4d>>()) {
        _DeclareAndConvertInstanceArrayTuple<float, GfVec4d>(node, name, _tokens->RGBA, AI_TYPE_RGBA, value, indices);
    }
}

inline bool _TokenStartsWithToken(const TfToken& t0, const TfToken& t1)
{
    return strncmp(t0.GetText(), t1.GetText(), t1.size()) == 0;
}

inline bool _CharStartsWithToken(const char* c, const TfToken& t) { return strncmp(c, t.GetText(), t.size()) == 0; }

inline void _SetRayFlag(const char* rayName, const VtValue& value, HdArnoldRayFlags* flags)
{
    if (flags == nullptr) {
        return;
    }
    auto flag = true;
    if (value.IsHolding<bool>()) {
        flag = value.UncheckedGet<bool>();
    } else if (value.IsHolding<int>()) {
        flag = value.UncheckedGet<int>() != 0;
    } else if (value.IsHolding<long>()) {
        flag = value.UncheckedGet<long>() != 0;
    } else {
        // Invalid value stored, exit.
        return;
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
    } else {
        // Invalid flag name, exit.
        return;
    }
    flags->SetPrimvarFlag(bitFlag, flag);
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

void HdArnoldSetTransform(AtNode* node, HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    HdArnoldSampledMatrixType xf{};
    sceneDelegate->SampleTransform(id, &xf);
    if (Ai_unlikely(xf.count == 0)) {
        AiNodeSetArray(node, str::matrix, AiArray(1, 1, AI_TYPE_MATRIX, AiM4Identity()));
        AiNodeResetParameter(node, str::motion_start);
        AiNodeResetParameter(node, str::motion_end);
        return;
    }
    AtArray* matrices = AiArrayAllocate(1, xf.count, AI_TYPE_MATRIX);
    for (auto i = decltype(xf.count){0}; i < xf.count; ++i) {
        AiArraySetMtx(matrices, i, HdArnoldConvertMatrix(xf.values[i]));
    }
    AiNodeSetArray(node, str::matrix, matrices);
    // We expect the samples to be sorted, and we reset motion start and motion end if there is only one sample.
    // This might be an [] in older USD versions, so not using standard container accessors.
    if (xf.count > 1) {
        AiNodeSetFlt(node, str::motion_start, xf.times[0]);
        AiNodeSetFlt(node, str::motion_end, xf.times[xf.count - 1]);
    } else {
        AiNodeResetParameter(node, str::motion_start);
        AiNodeResetParameter(node, str::motion_end);
    }
}

void HdArnoldSetTransform(const std::vector<AtNode*>& nodes, HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    HdArnoldSampledMatrixType xf{};
    sceneDelegate->SampleTransform(id, &xf);
    const auto nodeCount = nodes.size();
    if (Ai_unlikely(xf.count == 0)) {
        for (auto i = decltype(nodeCount){1}; i < nodeCount; ++i) {
            AiNodeSetArray(nodes[i], str::matrix, AiArray(1, 1, AI_TYPE_MATRIX, AiM4Identity()));
            AiNodeResetParameter(nodes[i], str::motion_start);
            AiNodeResetParameter(nodes[i], str::motion_end);
        }
        return;
    }
    AtArray* matrices = AiArrayAllocate(1, xf.count, AI_TYPE_MATRIX);
    for (auto i = decltype(xf.count){0}; i < xf.count; ++i) {
        AiArraySetMtx(matrices, i, HdArnoldConvertMatrix(xf.values[i]));
    }
    const auto motionStart = xf.times[0];
    const auto motionEnd = xf.times[xf.count - 1];
    auto setMotion = [&](AtNode* node) {
        if (xf.count > 1) {
            AiNodeSetFlt(node, str::motion_start, motionStart);
            AiNodeSetFlt(node, str::motion_end, motionEnd);
        } else {
            AiNodeResetParameter(node, str::motion_start);
            AiNodeResetParameter(node, str::motion_end);
        }
    };
    if (nodeCount > 0) {
        // You can't set the same array on two different nodes,
        // because it causes a double-free.
        // TODO(pal): we need to check if it's still the case with Arnold 5.
        for (auto i = decltype(nodeCount){1}; i < nodeCount; ++i) {
            AiNodeSetArray(nodes[i], str::matrix, AiArrayCopy(matrices));
            setMotion(nodes[i]);
        }
        AiNodeSetArray(nodes[0], str::matrix, matrices);
        setMotion(nodes[0]);
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
                if (_ConvertArray<float>(node, paramName, AI_TYPE_FLOAT, value) == 0) {
                    if (_ConvertArrayTyped<float, GfHalf>(node, paramName, AI_TYPE_FLOAT, value) == 0) {
                        _ConvertArrayTyped<float, double>(node, paramName, AI_TYPE_FLOAT, value);
                    }
                }
                break;
            case AI_TYPE_VECTOR2:
                if (_ConvertArray<GfVec2f>(node, paramName, arrayType, value) == 0) {
                    if (_ConvertArrayTuple<GfVec2f, GfVec2h>(node, paramName, arrayType, value) == 0) {
                        _ConvertArrayTuple<GfVec2f, GfVec2d>(node, paramName, arrayType, value);
                    }
                }
                break;
            case AI_TYPE_RGB:
            case AI_TYPE_VECTOR:
                if (_ConvertArray<GfVec3f>(node, paramName, arrayType, value) == 0) {
                    if (_ConvertArrayTuple<GfVec3f, GfVec3h>(node, paramName, arrayType, value) == 0) {
                        _ConvertArrayTuple<GfVec3f, GfVec3d>(node, paramName, arrayType, value);
                    }
                }
                break;
            case AI_TYPE_RGBA:
                if (_ConvertArray<GfVec4f>(node, paramName, arrayType, value) == 0) {
                    if (_ConvertArrayTuple<GfVec4f, GfVec4h>(node, paramName, arrayType, value) == 0) {
                        _ConvertArrayTuple<GfVec4f, GfVec4d>(node, paramName, arrayType, value);
                    }
                }
                break;
            case AI_TYPE_STRING:
                if (value.IsHolding<VtArray<std::string>>()) {
                    AiNodeSetArray(
                        node, paramName,
                        _ArrayConvert<std::string>(value.UncheckedGet<VtArray<std::string>>(), AI_TYPE_STRING));
                } else if (value.IsHolding<VtArray<TfToken>>()) {
                    AiNodeSetArray(
                        node, paramName,
                        _ArrayConvert<TfToken>(value.UncheckedGet<VtArray<TfToken>>(), AI_TYPE_STRING));
                } else if (value.IsHolding<VtArray<SdfAssetPath>>()) {
                    AiNodeSetArray(
                        node, paramName,
                        _ArrayConvert<SdfAssetPath>(value.UncheckedGet<VtArray<SdfAssetPath>>(), AI_TYPE_STRING));
                } else {
                    AiMsgError(
                        "Unsupported string array parameter %s.%s", AiNodeGetName(node),
                        AiParamGetName(pentry).c_str());
                }
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
            _SetFromValueOrArray<const GfVec3f&, const GfVec3h&, const GfVec3d&>(
                node, paramName, value, nodeSetRGBFromVec3, nodeSetRGBFromVec3h, nodeSetRGBFromVec3d);
            break;
        case AI_TYPE_RGBA:
            _SetFromValueOrArray<const GfVec4f&, const GfVec4h&, const GfVec4d&>(
                node, paramName, value, nodeSetRGBAFromVec4, nodeSetRGBAFromVec4h, nodeSetRGBAFromVec4d);
            break;
        case AI_TYPE_VECTOR:
            _SetFromValueOrArray<const GfVec3f&, const GfVec3h&, const GfVec3d&>(
                node, paramName, value, nodeSetVecFromVec3, nodeSetVecFromVec3h, nodeSetVecFromVec3d);
            break;
        case AI_TYPE_VECTOR2:
            _SetFromValueOrArray<const GfVec2f&, const GfVec2h&, const GfVec2d&>(
                node, paramName, value, nodeSetVec2FromVec2, nodeSetVec2FromVec2h, nodeSetVec2FromVec2d);
            break;
        case AI_TYPE_STRING:
            _SetFromValueOrArray<TfToken, const SdfAssetPath&, const std::string&>(
                node, paramName, value, nodeSetStrFromToken, nodeSetStrFromAssetPath, nodeSetStrFromStdStr);
            break;
        case AI_TYPE_POINTER:
        case AI_TYPE_NODE:
            break; // TODO(pal): Should be in the relationships list.
        case AI_TYPE_MATRIX:
            _SetFromValueOrArray<const GfMatrix4d&, const GfMatrix4f&>(
                node, paramName, value, nodeSetMatrixFromMatrix4d, nodeSetMatrixFromMatrix4f);
            break;
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

bool ConvertPrimvarToBuiltinParameter(
    AtNode* node, const TfToken& name, const VtValue& value, HdArnoldRayFlags* visibility, HdArnoldRayFlags* sidedness,
    HdArnoldRayFlags* autobumpVisibility)
{
    if (!_TokenStartsWithToken(name, _tokens->arnoldPrefix)) {
        return false;
    }

    const auto* paramName = name.GetText() + _tokens->arnoldPrefix.size();
    // We are checking if it's a visibility flag in form of
    // primvars:arnold:visibility:xyz where xyz is a name of a ray type.
    if (_CharStartsWithToken(paramName, _tokens->visibilityPrefix)) {
        const auto* rayName = paramName + _tokens->visibilityPrefix.size();
        _SetRayFlag(rayName, value, visibility);
        return true;
    }
    if (_CharStartsWithToken(paramName, _tokens->sidednessPrefix)) {
        const auto* rayName = paramName + _tokens->sidednessPrefix.size();
        _SetRayFlag(rayName, value, sidedness);
        return true;
    }
    if (_CharStartsWithToken(paramName, _tokens->autobumpVisibilityPrefix)) {
        const auto* rayName = paramName + _tokens->autobumpVisibilityPrefix.size();
        _SetRayFlag(rayName, value, autobumpVisibility);
        return true;
    }
    const auto* nodeEntry = AiNodeGetNodeEntry(node);
    const auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(paramName));
    if (paramEntry != nullptr) {
        HdArnoldSetParameter(node, paramEntry, value);
    }
    return true;
}

void HdArnoldSetConstantPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, HdArnoldRayFlags* visibility,
    HdArnoldRayFlags* sidedness, HdArnoldRayFlags* autobumpVisibility)
{
    // Remap primvars:arnold:xyz parameters to xyz parameters on the node.
    if (ConvertPrimvarToBuiltinParameter(node, name, value, visibility, sidedness, autobumpVisibility)) {
        return;
    }
    const auto isColor = role == HdPrimvarRoleTokens->color;
    if (name == HdPrimvarRoleTokens->color && isColor) {
        if (!_Declare(node, name, _tokens->constant, _tokens->RGBA)) {
            return;
        }

        if (value.IsHolding<GfVec4f>()) {
            const auto& v = value.UncheckedGet<GfVec4f>();
            AiNodeSetRGBA(node, AtString(name.GetText()), v[0], v[1], v[2], v[3]);
        } else if (value.IsHolding<VtVec4fArray>()) {
            const auto& arr = value.UncheckedGet<VtVec4fArray>();
            if (arr.empty()) {
                return;
            }
            const auto& v = arr[0];
            AiNodeSetRGBA(node, AtString(name.GetText()), v[0], v[1], v[2], v[3]);
        } else if (value.IsHolding<GfVec4h>()) {
            const auto& v = value.UncheckedGet<GfVec4h>();
            AiNodeSetRGBA(
                node, AtString(name.GetText()), static_cast<float>(v[0]), static_cast<float>(v[1]),
                static_cast<float>(v[2]), static_cast<float>(v[3]));
        } else if (value.IsHolding<VtVec4hArray>()) {
            const auto& arr = value.UncheckedGet<VtVec4hArray>();
            if (arr.empty()) {
                return;
            }
            const auto& v = arr[0];
            AiNodeSetRGBA(
                node, AtString(name.GetText()), static_cast<float>(v[0]), static_cast<float>(v[1]),
                static_cast<float>(v[2]), static_cast<float>(v[3]));
        } else if (value.IsHolding<GfVec4d>()) {
            const auto& v = value.UncheckedGet<GfVec4d>();
            AiNodeSetRGBA(
                node, AtString(name.GetText()), static_cast<float>(v[0]), static_cast<float>(v[1]),
                static_cast<float>(v[2]), static_cast<float>(v[3]));
        } else if (value.IsHolding<VtVec4dArray>()) {
            const auto& arr = value.UncheckedGet<VtVec4dArray>();
            if (arr.empty()) {
                return;
            }
            const auto& v = arr[0];
            AiNodeSetRGBA(
                node, AtString(name.GetText()), static_cast<float>(v[0]), static_cast<float>(v[1]),
                static_cast<float>(v[2]), static_cast<float>(v[3]));
        }
    }
    _DeclareAndAssignConstant(node, name, value, isColor);
}

void HdArnoldSetConstantPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const HdPrimvarDescriptor& primvarDesc,
    HdArnoldRayFlags* visibility, HdArnoldRayFlags* sidedness, HdArnoldRayFlags* autobumpVisibility)
{
    HdArnoldSetConstantPrimvar(
        node, primvarDesc.name, primvarDesc.role, sceneDelegate->Get(id, primvarDesc.name), visibility, sidedness,
        autobumpVisibility);
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
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const HdPrimvarDescriptor& primvarDesc)
{
    _DeclareAndAssignFromArray(
        node, primvarDesc.name, _tokens->varying, sceneDelegate->Get(id, primvarDesc.name),
        primvarDesc.role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, const VtIntArray* vertexCounts,
    const size_t* vertexCountSum)
{
    const auto numElements =
        _DeclareAndAssignFromArray(node, name, _tokens->indexed, value, role == HdPrimvarRoleTokens->color);
    if (numElements == 0) {
        return;
    }

    AiNodeSetArray(
        node, AtString(TfStringPrintf("%sidxs", name.GetText()).c_str()),
        HdArnoldGenerateIdxs(numElements, vertexCounts, vertexCountSum));
}

void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const HdPrimvarDescriptor& primvarDesc,
    const VtIntArray* vertexCounts, const size_t* vertexCountSum)
{
    HdArnoldSetFaceVaryingPrimvar(
        node, primvarDesc.name, primvarDesc.role, sceneDelegate->Get(id, primvarDesc.name), vertexCounts,
        vertexCountSum);
}

void HdArnoldSetInstancePrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtIntArray& indices, const VtValue& value)
{
    _DeclareAndAssignInstancePrimvar(
        node, TfToken{TfStringPrintf("instance_%s", name.GetText())}, value, role == HdPrimvarRoleTokens->color,
        indices);
}

size_t HdArnoldSetPositionFromPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const AtString& paramName)
{
    HdArnoldSampledPrimvarType xf;
    sceneDelegate->SamplePrimvar(id, HdTokens->points, &xf);
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
            AiArraySetKey(arr, index, vi.data());
        } else {
            AiArraySetKey(arr, index, v0.data());
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

void HdArnoldSetRadiusFromPrimvar(AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate)
{
    HdArnoldSampledPrimvarType xf;
    sceneDelegate->SamplePrimvar(id, HdTokens->widths, &xf);
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

AtArray* HdArnoldGenerateIdxs(unsigned int numIdxs, const VtIntArray* vertexCounts, const size_t* vertexCountSum)
{
    if (vertexCountSum != nullptr && numIdxs != *vertexCountSum) {
        return AiArrayAllocate(0, 1, AI_TYPE_UINT);
    }
    auto* array = AiArrayAllocate(numIdxs, 1, AI_TYPE_UINT);
    auto* out = static_cast<uint32_t*>(AiArrayMap(array));
    // Flip indices per polygon to support left handed topologies.
    if (vertexCounts != nullptr && !vertexCounts->empty()) {
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

void HdArnoldInsertPrimvar(
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

bool HdArnoldGetComputedPrimvars(
    HdSceneDelegate* delegate, const SdfPath& id, HdDirtyBits dirtyBits, HdArnoldPrimvarMap& primvars,
    const std::vector<HdInterpolation>* interpolations)
{
    // First we are querying which primvars need to be computed, and storing them in a list to rely on
    // the batched computation function in HdExtComputationUtils.
    HdExtComputationPrimvarDescriptorVector dirtyPrimvars;
    for (auto interpolation : (interpolations == nullptr ? primvarInterpolations : *interpolations)) {
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
        HdArnoldInsertPrimvar(primvars, primvar.name, primvar.role, primvar.interpolation, itComputed->second);
    }

    return changed;
}

void HdArnoldGetPrimvars(
    HdSceneDelegate* delegate, const SdfPath& id, HdDirtyBits dirtyBits, bool multiplePositionKeys,
    HdArnoldPrimvarMap& primvars, const std::vector<HdInterpolation>* interpolations)
{
    for (auto interpolation : (interpolations == nullptr ? primvarInterpolations : *interpolations)) {
        const auto primvarDescs = delegate->GetPrimvarDescriptors(id, interpolation);
        for (const auto& primvarDesc : primvarDescs) {
            if (primvarDesc.name == HdTokens->points) {
                continue;
            }
            // The number of motion keys has to be matched between points and normals, so
            HdArnoldInsertPrimvar(
                primvars, primvarDesc.name, primvarDesc.role, primvarDesc.interpolation,
                (multiplePositionKeys && primvarDesc.name == HdTokens->normals) ? VtValue{}
                                                                                : delegate->Get(id, primvarDesc.name));
        }
    }
}

AtArray* HdArnoldGetShidxs(const HdGeomSubsets& subsets, int numFaces, HdArnoldSubsets& arnoldSubsets)
{
    HdArnoldSubsets{}.swap(arnoldSubsets);
    const auto numSubsets = subsets.size();
    // Arnold stores shader indices in 1 byte unsigned integer, so we can only represent 255 subsets.
    if (numSubsets == 0 || numSubsets > 255) {
        return AiArray(0, 1, AI_TYPE_BYTE);
    }

    arnoldSubsets.reserve(numSubsets);
    auto* shidxsArray = AiArrayAllocate(numFaces, 1, AI_TYPE_BYTE);
    auto* shidxs = static_cast<uint8_t*>(AiArrayMap(shidxsArray));
    uint8_t subsetId = 0;
    std::fill(shidxs, shidxs + numFaces, numSubsets);
    for (const auto& subset : subsets) {
        arnoldSubsets.push_back(subset.materialId);
        for (auto id : subset.indices) {
            if (Ai_likely(id >= 0 && id < numFaces)) {
                shidxs[id] = subsetId;
            }
        }
        subsetId += 1;
    }
    AiArrayUnmap(shidxsArray);
    return shidxsArray;
}

PXR_NAMESPACE_CLOSE_SCOPE
