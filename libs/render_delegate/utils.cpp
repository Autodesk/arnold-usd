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

#include "debug_codes.h"
#include "render_delegate.h"

#include <type_traits>
#include <parameters_utils.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((arnoldVisibility, "arnold:visibility"))
    ((visibilityPrefix, "visibility:"))
    ((sidednessPrefix, "sidedness:"))
    ((autobumpVisibilityPrefix, "autobump_visibility:"))
);
// clang-format on

namespace {

const std::vector<HdInterpolation> primvarInterpolations{
    HdInterpolationConstant, HdInterpolationUniform,     HdInterpolationVarying,
    HdInterpolationVertex,   HdInterpolationFaceVarying, HdInterpolationInstance,
};

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
        if (!v.empty()) {
            std::transform(v.begin(), v.end(), reinterpret_cast<TO*>(AiArrayMap(arr)), [](const FROM& from) -> TO {
                return static_cast<TO>(from);
            });
            AiArrayUnmap(arr);
        }
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
        if (!v.empty()) {
            std::transform(v.begin(), v.end(), reinterpret_cast<TO*>(AiArrayMap(arr)), [](const FROM& from) -> TO {
                return TO{from};
            });
            AiArrayUnmap(arr);
        }
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
        if (!v.empty()) {
            auto* mapped = static_cast<AtString*>(AiArrayMap(arr));
            for (const auto& from : v) {
                _ConvertToString(*mapped, from);
                mapped += 1;
            }
            AiArrayUnmap(arr);
        }
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
    if (numIndices > 0) {
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
    }
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
        if (!HdArnoldDeclare(node, name, str::t_constant, type)) {
            return 0;
        }
        f(node, AtString{name.GetText()}, v[0]);
        return 1;
    }
    if (!HdArnoldDeclare(node, name, scope, type)) {
        return 0;
    }
    auto* arr = AiArrayAllocate(v.size(), 1, arnoldType);
    if (!v.empty()) {
        std::transform(v.begin(), v.end(), reinterpret_cast<TO*>(AiArrayMap(arr)), [](const FROM& from) -> TO {
            return static_cast<TO>(from);
        });
        AiArrayUnmap(arr);
    }
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
        if (!HdArnoldDeclare(node, name, str::t_constant, type)) {
            return 0;
        }
        f(node, AtString{name.GetText()}, v[0]);
        return 1;
    }
    if (!HdArnoldDeclare(node, name, scope, type)) {
        return 0;
    }
    auto* arr = AiArrayAllocate(v.size(), 1, arnoldType);
    if (!v.empty()) {
        std::transform(
            reinterpret_cast<const typename CFROM::ScalarType*>(v.data()),
            reinterpret_cast<const typename CFROM::ScalarType*>(v.data()) + v.size() * CFROM::dimension,
            reinterpret_cast<TO*>(AiArrayMap(arr)),
            [](const typename CFROM::ScalarType& from) -> TO { return static_cast<TO>(from); });
        AiArrayUnmap(arr);
    }
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
        if (!HdArnoldDeclare(node, name, str::t_constant, type)) {
            return 0;
        }
        f(node, AtString{name.GetText()}, v[0]);
        return 1;
    }
    if (!HdArnoldDeclare(node, name, scope, type)) {
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
    if (!HdArnoldDeclare(node, name, str::t_constantArray, type)) {
        return;
    }
    auto* arr = AiArrayAllocate(numIndices, 1, arnoldType);
    if (numIndices > 0) {
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
    }
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
    if (!HdArnoldDeclare(node, name, str::t_constantArray, type)) {
        return;
    }
    auto* arr = AiArrayAllocate(numIndices, 1, arnoldType);
    if (numIndices > 0) {
        auto* mapped = reinterpret_cast<TO*>(AiArrayMap(arr));
        auto* data = reinterpret_cast<const typename CFROM::ScalarType*>(v.data());
        // We need to loop first over eventual parent instances, then over current instances, then over eventual child instances
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
    }
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
    // We don't check for the return value of HdArnoldDeclare. Even if the attribute already existed
    // we still want to set the array attribute (e.g. arnold instancer & attribute instancer_visibility)
    HdArnoldDeclare(node, name, str::t_constantArray, type);
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
            node, name, scope, str::t_BOOL, AI_TYPE_BOOLEAN, value, isConstant, AiNodeSetBool);
    } else if (value.IsHolding<VtUCharArray>()) {
        return _DeclareAndConvertArray<VtUCharArray::value_type>(
            node, name, scope, str::t_BYTE, AI_TYPE_BYTE, value, isConstant, AiNodeSetByte);
    } else if (value.IsHolding<VtUIntArray>()) {
        return _DeclareAndConvertArray<unsigned int>(
            node, name, scope, str::t_UINT, AI_TYPE_UINT, value, isConstant, AiNodeSetUInt);
    } else if (value.IsHolding<VtIntArray>()) {
        return _DeclareAndConvertArray<int>(
            node, name, scope, str::t_INT, AI_TYPE_INT, value, isConstant, AiNodeSetInt);
    } else if (value.IsHolding<VtFloatArray>()) {
        return _DeclareAndConvertArray<float>(
            node, name, scope, str::t_FLOAT, AI_TYPE_FLOAT, value, isConstant, AiNodeSetFlt);
    } else if (value.IsHolding<VtVec2fArray>()) {
        return _DeclareAndConvertArray<const GfVec2f&>(
            node, name, scope, str::t_VECTOR2, AI_TYPE_VECTOR2, value, isConstant, nodeSetVec2FromVec2);
    } else if (value.IsHolding<VtVec3fArray>()) {
        if (isColor) {
            return _DeclareAndConvertArray<const GfVec3f&>(
                node, name, scope, str::t_RGB, AI_TYPE_RGB, value, isConstant, nodeSetRGBFromVec3);
        } else {
            return _DeclareAndConvertArray<const GfVec3f&>(
                node, name, scope, str::t_VECTOR, AI_TYPE_VECTOR, value, isConstant, nodeSetVecFromVec3);
        }
    } else if (value.IsHolding<VtVec4fArray>()) {
        return _DeclareAndConvertArray<const GfVec4f&>(
            node, name, scope, str::t_RGBA, AI_TYPE_RGBA, value, isConstant, nodeSetRGBAFromVec4);
    } else if (value.IsHolding<VtStringArray>()) {
        return _DeclareAndConvertArray<const std::string&>(
            node, name, scope, str::t_STRING, AI_TYPE_STRING, value, isConstant, nodeSetStrFromStdStr);
    } else if (value.IsHolding<VtTokenArray>()) {
        return _DeclareAndConvertArray<TfToken>(
            node, name, scope, str::t_STRING, AI_TYPE_STRING, value, isConstant, nodeSetStrFromToken);
    } else if (value.IsHolding<VtArray<SdfAssetPath>>()) {
        return _DeclareAndConvertArray<const SdfAssetPath&>(
            node, name, scope, str::t_STRING, AI_TYPE_STRING, value, isConstant, nodeSetStrFromAssetPath);
    } else if (value.IsHolding<VtArray<GfHalf>>()) { // HALF types
        return _DeclareAndConvertArrayTyped<float, GfHalf>(
            node, name, scope, str::t_FLOAT, AI_TYPE_FLOAT, value, isConstant, nodeSetFltFromHalf);
    } else if (value.IsHolding<VtArray<GfVec2h>>()) {
        return _DeclareAndConvertArrayTuple<float, const GfVec2h&>(
            node, name, scope, str::t_VECTOR2, AI_TYPE_VECTOR2, value, isConstant, nodeSetVec2FromVec2h);
    } else if (value.IsHolding<VtArray<GfVec3h>>()) {
        if (isColor) {
            return _DeclareAndConvertArrayTuple<float, const GfVec3h&>(
                node, name, scope, str::t_RGB, AI_TYPE_RGB, value, isConstant, nodeSetRGBFromVec3h);
        } else {
            return _DeclareAndConvertArrayTuple<float, const GfVec3h&>(
                node, name, scope, str::t_VECTOR, AI_TYPE_VECTOR, value, isConstant, nodeSetRGBFromVec3h);
        }
    } else if (value.IsHolding<VtArray<GfVec4h>>()) {
        return _DeclareAndConvertArrayTuple<float, const GfVec4h&>(
            node, name, scope, str::t_RGBA, AI_TYPE_RGBA, value, isConstant, nodeSetRGBAFromVec4h);
    } else if (value.IsHolding<VtArray<double>>()) { // double types
        return _DeclareAndConvertArrayTyped<float, double>(
            node, name, scope, str::t_FLOAT, AI_TYPE_FLOAT, value, isConstant, nodeSetFltFromDouble);
    } else if (value.IsHolding<VtArray<GfVec2d>>()) {
        return _DeclareAndConvertArrayTuple<float, const GfVec2d&>(
            node, name, scope, str::t_VECTOR2, AI_TYPE_VECTOR2, value, isConstant, nodeSetVec2FromVec2d);
    } else if (value.IsHolding<VtArray<GfVec3d>>()) {
        if (isColor) {
            return _DeclareAndConvertArrayTuple<float, const GfVec3d&>(
                node, name, scope, str::t_RGB, AI_TYPE_RGB, value, isConstant, nodeSetRGBFromVec3d);
        } else {
            return _DeclareAndConvertArrayTuple<float, const GfVec3d&>(
                node, name, scope, str::t_VECTOR, AI_TYPE_VECTOR, value, isConstant, nodeSetRGBFromVec3d);
        }
    } else if (value.IsHolding<VtArray<GfVec4d>>()) {
        return _DeclareAndConvertArrayTuple<float, const GfVec4d&>(
            node, name, scope, str::t_RGBA, AI_TYPE_RGBA, value, isConstant, nodeSetRGBAFromVec4d);
    }
    return 0;
}

inline void _DeclareAndAssignConstant(AtNode* node, const TfToken& name, const VtValue& value, bool isColor = false)
{
    auto declareConstant = [&node, &name](const TfToken& type) -> bool {
        return HdArnoldDeclare(node, name, str::t_constant, type);
    };
    if (value.IsHolding<bool>()) {
        if (!declareConstant(str::t_BOOL)) {
            return;
        }
        AiNodeSetBool(node, AtString(name.GetText()), value.UncheckedGet<bool>());
    } else if (value.IsHolding<uint8_t>()) {
        if (!declareConstant(str::t_BYTE)) {
            return;
        }
        AiNodeSetByte(node, AtString(name.GetText()), value.UncheckedGet<uint8_t>());
    } else if (value.IsHolding<unsigned int>()) {
        if (!declareConstant(str::t_UINT)) {
            return;
        }
        AiNodeSetUInt(node, AtString(name.GetText()), value.UncheckedGet<unsigned int>());
    } else if (value.IsHolding<int>()) {
        if (!declareConstant(str::t_INT)) {
            return;
        }
        AiNodeSetInt(node, AtString(name.GetText()), value.UncheckedGet<int>());
    } else if (value.IsHolding<float>()) {
        if (!declareConstant(str::t_FLOAT)) {
            return;
        }
        AiNodeSetFlt(node, AtString(name.GetText()), value.UncheckedGet<float>());
    } else if (value.IsHolding<double>()) {
        if (!declareConstant(str::t_FLOAT)) {
            return;
        }
        AiNodeSetFlt(node, AtString(name.GetText()), static_cast<float>(value.UncheckedGet<double>()));
    } else if (value.IsHolding<GfVec2f>()) {
        if (!declareConstant(str::t_VECTOR2)) {
            return;
        }
        nodeSetVec2FromVec2(node, AtString{name.GetText()}, value.UncheckedGet<GfVec2f>());
    } else if (value.IsHolding<GfVec3f>()) {
        if (isColor) {
            if (!declareConstant(str::t_RGB)) {
                return;
            }
            nodeSetRGBFromVec3(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3f>());
        } else {
            if (!declareConstant(str::t_VECTOR)) {
                return;
            }
            nodeSetVecFromVec3(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3f>());
        }
    } else if (value.IsHolding<GfVec4f>()) {
        if (!declareConstant(str::t_RGBA)) {
            return;
        }
        nodeSetRGBAFromVec4(node, AtString{name.GetText()}, value.UncheckedGet<GfVec4f>());
    } else if (value.IsHolding<GfHalf>()) {
        if (!declareConstant(str::t_FLOAT)) {
            return;
        }
        nodeSetFltFromHalf(node, AtString{name.GetText()}, value.UncheckedGet<GfHalf>());
    } else if (value.IsHolding<GfVec2h>()) {
        if (!declareConstant(str::t_VECTOR2)) {
            return;
        }
        nodeSetVec2FromVec2h(node, AtString{name.GetText()}, value.UncheckedGet<GfVec2h>());
    } else if (value.IsHolding<GfVec3h>()) {
        if (isColor) {
            if (!declareConstant(str::t_RGB)) {
                return;
            }
            nodeSetRGBFromVec3h(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3h>());
        } else {
            if (!declareConstant(str::t_VECTOR)) {
                return;
            }
            nodeSetVecFromVec3h(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3h>());
        }
    } else if (value.IsHolding<GfVec4h>()) {
        if (!declareConstant(str::t_RGBA)) {
            return;
        }
        nodeSetRGBAFromVec4h(node, AtString{name.GetText()}, value.UncheckedGet<GfVec4h>());
    } else if (value.IsHolding<double>()) {
        if (!declareConstant(str::t_FLOAT)) {
            return;
        }
        nodeSetFltFromDouble(node, AtString{name.GetText()}, value.UncheckedGet<double>());
    } else if (value.IsHolding<GfVec2d>()) {
        if (!declareConstant(str::t_VECTOR2)) {
            return;
        }
        nodeSetVec2FromVec2d(node, AtString{name.GetText()}, value.UncheckedGet<GfVec2d>());
    } else if (value.IsHolding<GfVec3d>()) {
        if (isColor) {
            if (!declareConstant(str::t_RGB)) {
                return;
            }
            nodeSetRGBFromVec3d(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3d>());
        } else {
            if (!declareConstant(str::t_VECTOR)) {
                return;
            }
            nodeSetVecFromVec3d(node, AtString{name.GetText()}, value.UncheckedGet<GfVec3d>());
        }
    } else if (value.IsHolding<GfVec4d>()) {
        if (!declareConstant(str::t_RGBA)) {
            return;
        }
        nodeSetRGBAFromVec4d(node, AtString{name.GetText()}, value.UncheckedGet<GfVec4d>());
    } else if (value.IsHolding<TfToken>()) {
        if (!declareConstant(str::t_STRING)) {
            return;
        }
        nodeSetStrFromToken(node, AtString{name.GetText()}, value.UncheckedGet<TfToken>());
    } else if (value.IsHolding<std::string>()) {
        if (!declareConstant(str::t_STRING)) {
            return;
        }
        nodeSetStrFromStdStr(node, AtString{name.GetText()}, value.UncheckedGet<std::string>());
    } else if (value.IsHolding<SdfAssetPath>()) {
        if (!declareConstant(str::t_STRING)) {
            return;
        }
        nodeSetStrFromAssetPath(node, AtString{name.GetText()}, value.UncheckedGet<SdfAssetPath>());
    } else {
        // Display color is a special case, where an array with a single
        // element should be translated to a single, constant RGB.
        if (name == str::t_displayColor && value.IsHolding<VtVec3fArray>()) {
            const auto& v = value.UncheckedGet<VtVec3fArray>();
            if (v.size() == 1) {
                if (declareConstant(str::t_RGB)) {
                    AiNodeSetRGB(node, AtString(name.GetText()), v[0][0], v[0][1], v[0][2]);
                    return;
                }
            }
        }
        _DeclareAndAssignFromArray(node, name, str::t_constantArray, value, isColor, true);
    }
}

inline void _DeclareAndAssignInstancePrimvar(
    AtNode* node, const TfToken& name, const VtValue& value, bool isColor, const VtIntArray& indices)
{
    if (value.IsHolding<VtBoolArray>()) {
        _DeclareAndConvertInstanceArray<bool>(node, name, str::t_BOOL, AI_TYPE_BOOLEAN, value, indices);
    } else if (value.IsHolding<VtUCharArray>()) {
        _DeclareAndConvertInstanceArray<VtUCharArray::value_type>(
            node, name, str::t_BYTE, AI_TYPE_BYTE, value, indices);
    } else if (value.IsHolding<VtUIntArray>()) {
        _DeclareAndConvertInstanceArray<unsigned int>(node, name, str::t_UINT, AI_TYPE_UINT, value, 
            indices);
    } else if (value.IsHolding<VtIntArray>()) {
        _DeclareAndConvertInstanceArray<int>(node, name, str::t_INT, AI_TYPE_INT, value, indices);
    } else if (value.IsHolding<VtFloatArray>()) {
        _DeclareAndConvertInstanceArray<float>(node, name, str::t_FLOAT, AI_TYPE_FLOAT, value, indices);
    } else if (value.IsHolding<VtVec2fArray>()) {
        _DeclareAndConvertInstanceArray<const GfVec2f&>(node, name, str::t_VECTOR2, AI_TYPE_VECTOR2, value,
            indices);
    } else if (value.IsHolding<VtVec3fArray>()) {
        if (isColor) {
            _DeclareAndConvertInstanceArray<const GfVec3f&>(node, name, str::t_RGB, AI_TYPE_RGB, value,
                indices);
        } else {
            _DeclareAndConvertInstanceArray<const GfVec3f&>(node, name, str::t_VECTOR, AI_TYPE_VECTOR, value,
                indices);
        }
    } else if (value.IsHolding<VtVec4fArray>()) {
        _DeclareAndConvertInstanceArray<const GfVec4f&>(node, name, str::t_RGBA, AI_TYPE_RGBA, value, indices);
    } else if (value.IsHolding<VtStringArray>()) {
        _DeclareAndConvertInstanceArray<const std::string&>(node, name, str::t_STRING, AI_TYPE_STRING, value, 
            indices);
    } else if (value.IsHolding<VtTokenArray>()) {
        _DeclareAndConvertInstanceArray<TfToken>(node, name, str::t_STRING, AI_TYPE_STRING, value, indices);
    } else if (value.IsHolding<VtArray<SdfAssetPath>>()) {
        _DeclareAndConvertInstanceArray<const SdfAssetPath&>(node, name, str::t_STRING, AI_TYPE_STRING, value, 
            indices);
    } else if (value.IsHolding<VtArray<GfHalf>>()) { // Half types
        _DeclareAndConvertInstanceArrayTyped<float, GfHalf>(node, name, str::t_FLOAT, AI_TYPE_FLOAT, value, 
            indices);
    } else if (value.IsHolding<VtArray<GfVec2h>>()) {
        _DeclareAndConvertInstanceArrayTuple<float, GfVec2h>(
            node, name, str::t_VECTOR2, AI_TYPE_VECTOR2, value, indices);
    } else if (value.IsHolding<VtArray<GfVec3h>>()) {
        if (isColor) {
            _DeclareAndConvertInstanceArrayTuple<float, GfVec3h>(node, name, str::t_RGB, AI_TYPE_RGB, value, 
                indices);
        } else {
            _DeclareAndConvertInstanceArrayTuple<float, GfVec3h>(
                node, name, str::t_VECTOR, AI_TYPE_VECTOR, value, indices);
        }
    } else if (value.IsHolding<VtArray<GfVec4h>>()) {
        _DeclareAndConvertInstanceArrayTuple<float, GfVec4h>(node, name, str::t_RGBA, AI_TYPE_RGBA, value, 
            indices);
    } else if (value.IsHolding<VtArray<double>>()) { // double types
        _DeclareAndConvertInstanceArrayTyped<float, double>(node, name, str::t_FLOAT, AI_TYPE_FLOAT, value, 
            indices);
    } else if (value.IsHolding<VtArray<GfVec2d>>()) {
        _DeclareAndConvertInstanceArrayTuple<float, GfVec2d>(
            node, name, str::t_VECTOR2, AI_TYPE_VECTOR2, value, indices);
    } else if (value.IsHolding<VtArray<GfVec3d>>()) {
        if (isColor) {
            _DeclareAndConvertInstanceArrayTuple<float, GfVec3d>(node, name, str::t_RGB, AI_TYPE_RGB, value, 
                indices);
        } else {
            _DeclareAndConvertInstanceArrayTuple<float, GfVec3d>(
                node, name, str::t_VECTOR, AI_TYPE_VECTOR, value, indices);
        }
    } else if (value.IsHolding<VtArray<GfVec4d>>()) {
        _DeclareAndConvertInstanceArrayTuple<float, GfVec4d>(node, name, str::t_RGBA, AI_TYPE_RGBA, value, 
            indices);
    }
}

inline bool _TokenStartsWithToken(const TfToken& t0, const TfToken& t1)
{
    return strncmp(t0.GetText(), t1.GetText(), t1.size()) == 0;
}

inline bool _CharStartsWithToken(const char* c, const TfToken& t) { return strncmp(c, t.GetText(), t.size()) == 0; }

inline size_t _ExtrapolatePositions(
    AtNode* node, const AtString& paramName, HdArnoldSampledType<VtVec3fArray>& xf, const HdArnoldRenderParam* param,
    int deformKeys, const HdArnoldPrimvarMap* primvars)
{
    // If velocity or acceleration primvars are present, we want to use them to extrapolate 
    // the positions for motion blur, instead of relying on positions at different time samples. 
    // This allow to support varying topologies with motion blur
    if (primvars == nullptr || Ai_unlikely(param == nullptr) || param->InstananeousShutter()) {
        return 0;
    }

    // Check if primvars or positions exists. These arrays are COW.
    VtVec3fArray velocities;
    VtVec3fArray accelerations;
    auto primvarIt = primvars->find(HdTokens->velocities);
    if (primvarIt != primvars->end() && primvarIt->second.value.IsHolding<VtVec3fArray>()) {
        velocities = primvarIt->second.value.UncheckedGet<VtVec3fArray>();
    }
    primvarIt = primvars->find(HdTokens->accelerations);
    if (primvarIt != primvars->end() && primvarIt->second.value.IsHolding<VtVec3fArray>()) {
        accelerations = primvarIt->second.value.UncheckedGet<VtVec3fArray>();
    }

    // The positions in xf contain several several time samples, but the amount of vertices 
    // can change in each sample. We want to consider the positions at the proper time, so 
    // that we can apply the velocities/accelerations
    // First, let's check if one of the times is 0 (current frame)
    int timeIndex = -1;
    for (size_t i = 0; i < xf.times.size(); ++i) {
        if (xf.times[i] == 0) {
            timeIndex = i;
            break;
        }
    }
    // If no proper time was found, let's pick the first sample that has the same
    // size as the velocities
    size_t velocitiesSize = velocities.size();
    if (timeIndex < 0) {
        for (size_t i = 0; i < xf.values.size(); ++i) {
            if (velocitiesSize > 0 && xf.values[i].size() == velocitiesSize) {
                timeIndex = i;
                break;
            }
        }    
    }
    // If we still couldn't find a proper time, let's pick the first sample that has the same
    // size as the accelerations    
    size_t accelerationsSize = accelerations.size();
    if (timeIndex < 0) {
        for (size_t i = 0; i < xf.values.size(); ++i) {
            if (accelerationsSize > 0 && xf.values[i].size() == accelerationsSize) {
                timeIndex = i;
                break;
            }
        }    
    }

    if (timeIndex < 0) 
        return 0; // We couldn't find a proper time sample to read positions
    
    const auto& positions = xf.values[timeIndex];
    const auto numPositions = positions.size();
    const auto hasVelocity = !velocities.empty() && numPositions == velocities.size();
    const auto hasAcceleration = !accelerations.empty() && numPositions == accelerations.size();
    
    if (!hasVelocity && !hasAcceleration) {
        // No velocity or acceleration, or incorrect sizes for both.
        return 0;
    }
    const auto& t0 = xf.times[timeIndex];
    auto shutter = param->GetShutterRange();
    const auto numKeys = hasAcceleration ? deformKeys : std::min(2, deformKeys);
    TfSmallVector<float, HD_ARNOLD_MAX_PRIMVAR_SAMPLES> times;
    times.resize(numKeys);
    if (numKeys == 1) {
        times[0]  = 0.0;
    } else {
        times[0] = shutter[0];
        for (auto i = decltype(numKeys){1}; i < numKeys - 1; i += 1) {
            times[i] = AiLerp(static_cast<float>(i) / static_cast<float>(numKeys - 1), shutter[0], shutter[1]);
        }
        times[numKeys - 1] = shutter[1];
    }
    const auto fps = 1.0f / param->GetFPS();
    const auto fps2 = fps * fps;
    auto* array = AiArrayAllocate(numPositions, numKeys, AI_TYPE_VECTOR);
    if (numPositions > 0 && numKeys > 0) {
        auto* data = reinterpret_cast<GfVec3f*>(AiArrayMap(array));
        for (auto pid = decltype(numPositions){0}; pid < numPositions; pid += 1) {
            const auto p = positions[pid];
            const auto v = hasVelocity ? velocities[pid] * fps : GfVec3f{0.0f};
            const auto a = hasAcceleration ? accelerations[pid] * fps2 : GfVec3f{0.0f};
            for (auto tid = decltype(numKeys){0}; tid < numKeys; tid += 1) {
                const auto t = t0 + times[tid];
                data[pid + tid * numPositions] = p + (v + a * t * 0.5f) * t;
            }
        }
        AiArrayUnmap(array);
    }
    AiNodeSetArray(node, paramName, array);
    return numKeys;
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

void HdArnoldSetParameter(AtNode* node, const AtParamEntry* pentry, const VtValue& value, 
    HdArnoldRenderDelegate *renderDelegate)
{
    if (value.IsEmpty())
        return;

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
                    AiMsgWarning(
                        "Unsupported string array parameter %s.%s", AiNodeGetName(node),
                        AiParamGetName(pentry).c_str());
                }
                break;
            case AI_TYPE_POINTER:   /**< Arbitrary pointer */
            case AI_TYPE_NODE:      /**< Pointer to an Arnold node */
                if (value.IsHolding<VtArray<std::string>>()) {
                    const auto& v = value.UncheckedGet<VtArray<std::string>>();
                    // Iterate on VtArray and find the nodes. If some of the nodes are missing, report them.
                    // In Hydra we expect all the nodes to be created in the constructor of the HdPrims, so they should exist when this function is called.
                    // It this function is not able to set the nodes, then an error should be reported
                    if (v.size()) {
                        auto* arr = AiArrayAllocate(v.size(), 1, AI_TYPE_NODE);
                        for (int i=0; i<v.size(); ++i) {
                            // The node can also have another name, specified in arnold:name attribute, however in our hydra implementation
                            // we don't support custom names, so we don't need to remap here
                            AtNode *targetNode = renderDelegate->LookupNode(v[i].c_str());
                            AiArraySetPtr(arr, i, targetNode);
                        }
                        AiNodeSetArray(node, paramName, arr);
                    }
                // Not handling arrays of SdfAssetPath
                //  } else if (value.IsHolding<VtArray<SdfAssetPath>>()) {
                } else {
                    AiMsgWarning(
                        "Unsupported node array parameter %s.%s", AiNodeGetName(node),
                        AiParamGetName(pentry).c_str());
                }
                break;

            case AI_TYPE_MATRIX:    /**< 4x4 matrix */
                // Convert array of matrices
                if (value.IsHolding<VtArray<GfMatrix4d>>()) {
                    const auto& v = value.UncheckedGet<VtArray<GfMatrix4d>>();
                    if (v.size()) {
                        AtArray* matrices = AiArrayAllocate(v.size(), 1, AI_TYPE_MATRIX);
                        for (int i = 0; i < v.size(); ++i) {
                            AiArraySetMtx(matrices, i, HdArnoldConvertMatrix(v[i]));
                        }
                        AiNodeSetArray(node, paramName, matrices);
                    }
                } else if (value.IsHolding<VtArray<GfMatrix4f>>()) {
                    const auto& v = value.UncheckedGet<VtArray<GfMatrix4f>>();
                    if (v.size()) {
                        AtArray* matrices = AiArrayAllocate(v.size(), 1, AI_TYPE_MATRIX);
                        for (int i = 0; i < v.size(); ++i) {
                            AiArraySetMtx(matrices, i, HdArnoldConvertMatrix(v[i]));
                        }
                        AiNodeSetArray(node, paramName, matrices);
                    }
                }
                break;
            case AI_TYPE_BYTE:      /**< uint8_t (an 8-bit sized unsigned integer) */
                _ConvertArray<unsigned char>(node, paramName, AI_TYPE_BYTE, value);
                break;
            case AI_TYPE_CLOSURE:   /**< Shader closure */
            case AI_TYPE_USHORT:    /**< unsigned short (16-bit unsigned integer) (used by drivers only) */
            case AI_TYPE_UNDEFINED: /**< Undefined, you should never encounter a parameter of this type */
            default:
                AiMsgWarning("Unsupported array parameter %s.%s", AiNodeGetName(node), AiParamGetName(pentry).c_str());
        }
        return;
    }
    switch (paramType) {
        case AI_TYPE_BYTE:
            AiNodeSetByte(node, paramName, VtValueGetByte(value));
            break;
        case AI_TYPE_INT:
            AiNodeSetInt(node, paramName, VtValueGetInt(value));
            break;
        case AI_TYPE_UINT:
        case AI_TYPE_USHORT:
            AiNodeSetUInt(node, paramName, VtValueGetUInt(value));
            break;
        case AI_TYPE_BOOLEAN:
            AiNodeSetBool(node, paramName, VtValueGetBool(value));
            break;
        case AI_TYPE_FLOAT:
        case AI_TYPE_HALF:
            AiNodeSetFlt(node, paramName, VtValueGetFloat(value));
            break;
        case AI_TYPE_RGB: {
            GfVec3f vec = VtValueGetVec3f(value);
            AiNodeSetRGB(node, paramName, vec[0], vec[1], vec[2]);
            break;
        }
        case AI_TYPE_RGBA: {
            GfVec4f vec = VtValueGetVec4f(value);
            AiNodeSetRGBA(node, paramName, vec[0], vec[1], vec[2], vec[3]);
            break;
        }
        case AI_TYPE_VECTOR: {
            GfVec3f vec = VtValueGetVec3f(value);
            AiNodeSetVec(node, paramName, vec[0], vec[1], vec[2]);
            break;
        }
        case AI_TYPE_VECTOR2: {
            GfVec2f vec = VtValueGetVec2f(value);
            AiNodeSetVec2(node, paramName, vec[0], vec[1]);
            break;
        }
        case AI_TYPE_ENUM:
            if (value.IsHolding<int>()) {
                AiNodeSetInt(node, paramName, value.UncheckedGet<int>());
                break;
            } else if (value.IsHolding<long>()) {
                AiNodeSetInt(node, paramName, value.UncheckedGet<long>());
                break;
            }
            // Enums can be strings, so we don't break here.
        case AI_TYPE_STRING: {
            std::string str = VtValueGetString(value);
            AiNodeSetStr(node, paramName, AtString(str.c_str()));
            break;
        }
        case AI_TYPE_POINTER:
        case AI_TYPE_NODE:
            break; // TODO(pal): Should be in the relationships list.
        case AI_TYPE_MATRIX: {
            AtMatrix aiMat;
            if (VtValueGetMatrix(value, aiMat))
                AiNodeSetMatrix(node, paramName, aiMat);
            break;
        }
        case AI_TYPE_CLOSURE:
            break; // Should be in the relationships list.
        default:
            AiMsgWarning("Unsupported parameter %s.%s", AiNodeGetName(node), AiParamGetName(pentry).c_str());
    }
}


bool ConvertPrimvarToRayFlag(AtNode* node, const TfToken& name, const VtValue& value, HdArnoldRayFlags* visibility, 
    HdArnoldRayFlags* sidedness, HdArnoldRayFlags* autobumpVisibility)
{
    if (!_TokenStartsWithToken(name, str::t_arnold_prefix)) {
        return false;
    }

    // In addition to parameters like arnold:visibility:camera, etc...
    // we also want to support arnold:visibility as this is what the writer 
    // will author. Note that we could be trying to set this attribute on a node 
    // that doesn't have any visibility attribute (e.g. a light), so we need to check
    // the HdArnoldRayFlags pointer exists (see #1535)
    if (visibility && name == _tokens->arnoldVisibility) {
        uint8_t visibilityValue = 0;
        if (value.IsHolding<int>()) {
            visibilityValue = value.Get<int>();
        } 
        AiNodeSetByte(node, str::visibility, visibilityValue);
        // In this case we want to force the visibility to be this current value.
        // So we first need to remove any visibility flag, and then we set the new one
        visibility->SetPrimvarFlag(AI_RAY_ALL, false);
        visibility->SetPrimvarFlag(visibilityValue, true);
        return true;
    }
    const auto* paramName = name.GetText() + str::t_arnold_prefix.size();    
    // We are checking if it's a visibility flag in form of
    // primvars:arnold:visibility:xyz where xyz is a name of a ray type.
    auto charStartsWithToken = [&](const char *c, const TfToken& t) { return strncmp(c, t.GetText(), t.size()) == 0; };

    if (visibility && charStartsWithToken(paramName, _tokens->visibilityPrefix)) {
        const auto* rayName = paramName + _tokens->visibilityPrefix.size();
        visibility->SetRayFlag(rayName, value);
        return true;
    }
    if (sidedness && charStartsWithToken(paramName, _tokens->sidednessPrefix)) {
        const auto* rayName = paramName + _tokens->sidednessPrefix.size();
        sidedness->SetRayFlag(rayName, value);
        return true;
    }
    if (autobumpVisibility && charStartsWithToken(paramName, _tokens->autobumpVisibilityPrefix)) {
        const auto* rayName = paramName + _tokens->autobumpVisibilityPrefix.size();
        autobumpVisibility->SetRayFlag(rayName, value);
        return true;
    }
    // This attribute wasn't meant for one of the 3 ray flag attributes
    return false;
}

bool ConvertPrimvarToBuiltinParameter(
    AtNode* node, const TfToken& name, const VtValue& value, HdArnoldRayFlags* visibility, HdArnoldRayFlags* sidedness,
    HdArnoldRayFlags* autobumpVisibility, HdArnoldRenderDelegate *renderDelegate)
{
    if (!_TokenStartsWithToken(name, str::t_arnold_prefix)) {
        return false;
    }

    // In addition to parameters like arnold:visibility:camera, etc...
    // we also want to support arnold:visibility as this is what the arnold-usd writer 
    // will author
    if (visibility && name == _tokens->arnoldVisibility) {
        uint8_t visibilityValue = value.Get<int>();
        AiNodeSetByte(node, str::visibility, visibilityValue);
        // In this case we want to force the visibility to be this current value.
        // So we first need to remove any visibility flag, and then we set the new one
        visibility->SetPrimvarFlag(AI_RAY_ALL, false);
        visibility->SetPrimvarFlag(visibilityValue, true);
        return true;
    }

    if (ConvertPrimvarToRayFlag(node, name, value, visibility, sidedness, autobumpVisibility)) {
        return true;
    }

    // Extract the arnold prefix from the primvar name
    const auto* paramName = name.GetText() + str::t_arnold_prefix.size();    
    const auto* nodeEntry = AiNodeGetNodeEntry(node);
    const auto* paramEntry = AiNodeEntryLookUpParameter(nodeEntry, AtString(paramName));
    if (paramEntry != nullptr) {
        HdArnoldSetParameter(node, paramEntry, value, renderDelegate);
    }
    return true;
}

void HdArnoldSetConstantPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, HdArnoldRayFlags* visibility,
    HdArnoldRayFlags* sidedness, HdArnoldRayFlags* autobumpVisibility, HdArnoldRenderDelegate *renderDelegate)
{
    // Remap primvars:arnold:xyz parameters to xyz parameters on the node.
    if (ConvertPrimvarToBuiltinParameter(node, name, value, visibility, sidedness, autobumpVisibility, renderDelegate)) {
        return;
    }
    const auto isColor = role == HdPrimvarRoleTokens->color;
    if (name == HdPrimvarRoleTokens->color && isColor) {
        if (!HdArnoldDeclare(node, name, str::t_constant, str::t_RGBA)) {
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
    HdArnoldRayFlags* visibility, HdArnoldRayFlags* sidedness, HdArnoldRayFlags* autobumpVisibility, 
    HdArnoldRenderDelegate *renderDelegate)
{
    HdArnoldSetConstantPrimvar(
        node, primvarDesc.name, primvarDesc.role, sceneDelegate->Get(id, primvarDesc.name), visibility, sidedness,
        autobumpVisibility, renderDelegate);
}

void HdArnoldSetUniformPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value)
{
    _DeclareAndAssignFromArray(node, name, str::t_uniform, value, role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetUniformPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc)
{
    _DeclareAndAssignFromArray(
        node, primvarDesc.name, str::t_uniform, delegate->Get(id, primvarDesc.name),
        primvarDesc.role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetVertexPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value)
{
    _DeclareAndAssignFromArray(node, name, str::t_varying, value, role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetVertexPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const HdPrimvarDescriptor& primvarDesc)
{
    _DeclareAndAssignFromArray(
        node, primvarDesc.name, str::t_varying, sceneDelegate->Get(id, primvarDesc.name),
        primvarDesc.role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value,
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
    const VtIntArray& valueIndices,
#endif
    const VtIntArray* vertexCounts, const size_t* vertexCountSum)
{
    const auto numElements =
        _DeclareAndAssignFromArray(node, name, str::t_indexed, value, role == HdPrimvarRoleTokens->color);
    // 0 means the array can't be extracted from the VtValue.
    // 1 means the array had a single element, and it was set as a constant user data.
    if (numElements <= 1) {
        return;
    }

    auto* indices =
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        !valueIndices.empty() ? HdArnoldGenerateIdxs(valueIndices, vertexCounts) :
#endif
                              HdArnoldGenerateIdxs(numElements, vertexCounts, vertexCountSum);

    AiNodeSetArray(node, AtString(TfStringPrintf("%sidxs", name.GetText()).c_str()), indices);
}

void HdArnoldSetInstancePrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtIntArray& indices, const VtValue& value)
{
    _DeclareAndAssignInstancePrimvar(
        node, TfToken{TfStringPrintf("instance_%s", name.GetText())}, value, role == HdPrimvarRoleTokens->color,
        indices);
}

size_t HdArnoldSetPositionFromPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const AtString& paramName,
    const HdArnoldRenderParam* param, int deformKeys, const HdArnoldPrimvarMap* primvars, HdArnoldSampledPrimvarType *pointsSample)
{
    HdArnoldSampledPrimvarType sample;
    if (pointsSample != nullptr && pointsSample->count > 0)
        sample = *pointsSample;
    else
        sceneDelegate->SamplePrimvar(id, HdTokens->points, &sample);

    HdArnoldSampledType<VtVec3fArray> xf;
    HdArnoldUnboxSample(sample, xf);
    if (xf.count == 0) {
        return 0;
    }
    const auto& v0 = xf.values[0];
    if (Ai_unlikely(v0.empty())) {
        return 0;
    }
    // Check if we can/should extrapolate positions based on velocities/accelerations.
    const auto extrapolatedCount = _ExtrapolatePositions(node, paramName, xf, param, deformKeys, primvars);
    if (extrapolatedCount != 0) {
        return extrapolatedCount;
    }
    bool varyingTopology = false;
    for (const auto &value : xf.values) {
        if (value.size() != v0.size()) {
            varyingTopology = true;
            break;
        }
    }
    if (!varyingTopology) {
        auto* arr = AiArrayAllocate(v0.size(), xf.count, AI_TYPE_VECTOR);
        for (size_t index = 0; index < xf.count; index++) {
            auto t = xf.times[0];
            if (xf.count > 1)
                t += index * (xf.times[xf.count-1] - xf.times[0]) / (static_cast<float>(xf.count)-1.f);
            const auto data = xf.Resample(t);
            AiArraySetKey(arr, index, data.data());
        }  
        AiNodeSetArray(node, paramName, arr);
        return xf.count;
    }

    // Varying topology, and no velocity. Let's choose which time sample to pick.
    // Ideally we'd want time = 0, as this is what will correspond to the amount of 
    // expected vertices in other static arrays (like vertex indices). But we might
    // not always have this time in our list, so we'll use the first positive time
    int timeIndex = 0;
    for (size_t i = 0; i < xf.times.size(); ++i) {
        if (xf.times[i] >= 0) {
            timeIndex = i;
            break;
        }
    }

    // Let's raise an error as this is going to cause problems during rendering
    if (xf.count > 1) 
        AiMsgError("%-30s | Number of vertices changed between motion steps", AiNodeGetName(node));
    
    // Just export a single key since the number of vertices change along the shutter range,
    // and we don't have any velocity / acceleration data
    auto* arr = AiArrayAllocate(xf.values[timeIndex].size(), 1, AI_TYPE_VECTOR);
    AiArraySetKey(arr, 0, xf.values[timeIndex].data());
    AiNodeSetArray(node, paramName, arr);

    return 1;

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
    HdArnoldSampledPrimvarType sample;
    sceneDelegate->SamplePrimvar(id, HdTokens->widths, &sample);
    HdArnoldSampledType<VtFloatArray> xf;
    HdArnoldUnboxSample(sample, xf);
    if (xf.count == 0) {
        return;
    }

    int timeIndex = 0;
    for (size_t i = 0; i < xf.times.size(); ++i) {
        if (xf.times[i] >= 0) {
            timeIndex = i;
            break;
        }
    }
    const auto& v0 = xf.values[timeIndex];
    auto* arr = AiArrayAllocate(v0.size(), 1, AI_TYPE_FLOAT);
    auto* out = static_cast<float*>(AiArrayMap(arr));
    auto convertWidth = [](const float w) -> float { return w * 0.5f; };
    std::transform(v0.begin(), v0.end(), out, convertWidth);
    AiNodeSetArray(node, str::radius, arr);
}

AtArray* HdArnoldGenerateIdxs(unsigned int numIdxs, const VtIntArray* vertexCounts, const size_t* vertexCountSum)
{
    if (vertexCountSum != nullptr && numIdxs != *vertexCountSum) {
        return AiArrayAllocate(0, 1, AI_TYPE_UINT);
    }
    auto* array = AiArrayAllocate(numIdxs, 1, AI_TYPE_UINT);
    if (numIdxs > 0) {
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
    }
    return array;
}

AtArray* HdArnoldGenerateIdxs(const VtIntArray& indices, const VtIntArray* vertexCounts)
{
    const auto numIdxs = static_cast<uint32_t>(indices.size());
    if (numIdxs < 3) {
        return AiArrayAllocate(0, 1, AI_TYPE_UINT);
    }
    auto* array = AiArrayAllocate(numIdxs, 1, AI_TYPE_UINT);
    if (numIdxs > 0) {
        auto* out = static_cast<uint32_t*>(AiArrayMap(array));
        if (vertexCounts != nullptr && !vertexCounts->empty()) {
            unsigned int vertexId = 0;
            for (auto vertexCount : *vertexCounts) {
                if (Ai_unlikely(vertexCount <= 0) || Ai_unlikely(vertexId + vertexCount > numIdxs)) {
                    continue;
                }
                for (auto vertex = decltype(vertexCount){0}; vertex < vertexCount; vertex += 1) {
                    out[vertexId + vertex] = indices[vertexId + vertexCount - vertex - 1];
                }
                vertexId += vertexCount;
            }
        } else {
            std::copy(indices.begin(), indices.end(), out);
        }

        AiArrayUnmap(array);
    }
    return array;
}

void HdArnoldInsertPrimvar(
    HdArnoldPrimvarMap& primvars, const TfToken& name, const TfToken& role, HdInterpolation interpolation,
    const VtValue& value
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
    ,
    const VtIntArray& valueIndices
#endif
)
{
    auto it = primvars.find(name);
    if (it == primvars.end()) {
        primvars.insert({name,
                         {value,
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
                          valueIndices,
#endif
                          role, interpolation}});
    } else {
        it->second.value = value;
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        it->second.valueIndices = valueIndices;
#endif
        it->second.role = role;
        it->second.interpolation = interpolation;
        it->second.dirtied = true;
    }
}

bool HdArnoldGetComputedPrimvars(
    HdSceneDelegate* delegate, const SdfPath& id, HdDirtyBits dirtyBits, HdArnoldPrimvarMap& primvars,
    const std::vector<HdInterpolation>* interpolations, HdArnoldSampledPrimvarType *pointsSample)
{
    // First we are querying which primvars need to be computed, and storing them in a list to rely on
    // the batched computation function in HdExtComputationUtils.
    HdExtComputationPrimvarDescriptorVector dirtyPrimvars;
    HdExtComputationPrimvarDescriptorVector pointsPrimvars;
    for (auto interpolation : (interpolations == nullptr ? primvarInterpolations : *interpolations)) {
        const auto computedPrimvars = delegate->GetExtComputationPrimvarDescriptors(id, interpolation);
        for (const auto& primvar : computedPrimvars) {
            if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, primvar.name)) {
#if PXR_VERSION >= 2105
                if (primvar.name == HdTokens->points)
                    pointsPrimvars.emplace_back(primvar);
                else
#endif
                {

                    dirtyPrimvars.emplace_back(primvar);
                }
            }
        }
    }
    
    bool changed = false;
#if PXR_VERSION >= 2105
    if (pointsSample && !pointsPrimvars.empty()) {
        HdExtComputationUtils::SampledValueStore<HD_ARNOLD_MAX_PRIMVAR_SAMPLES> valueStore;
        const size_t maxSamples = HD_ARNOLD_MAX_PRIMVAR_SAMPLES;
        HdExtComputationUtils::SampleComputedPrimvarValues(
            pointsPrimvars, delegate, maxSamples, &valueStore);
        
        const auto itComputed = valueStore.find(pointsPrimvars[0].name);
            
        if (itComputed != valueStore.end() && itComputed->second.count > 0) {
            changed = true;
            // Store points separately, with sampled results
            *pointsSample = itComputed->second;
        }
    }
#endif

    if (!dirtyPrimvars.empty()) {

        auto valueStore = HdExtComputationUtils::GetComputedPrimvarValues(dirtyPrimvars, delegate);

        for (const auto& primvar : dirtyPrimvars) {
            const auto itComputed = valueStore.find(primvar.name);
            if (itComputed == valueStore.end()) {
                continue;
            }
            changed = true;
            
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
            HdArnoldInsertPrimvar(primvars, primvar.name, primvar.role, primvar.interpolation, itComputed->second, {});
#else
            HdArnoldInsertPrimvar(primvars, primvar.name, primvar.role, primvar.interpolation, itComputed->second);
#endif
        }
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
            // Point positions either come from computed primvars using a different function or have a dedicated
            // dirty bit.
            if (primvarDesc.name == HdTokens->points) {
                continue;
            }
            // The number of motion keys has to be matched between points and normals, so if there are multiple
            // position keys, so we are forcing the user to use the SamplePrimvars function.
            if (multiplePositionKeys && primvarDesc.name == HdTokens->normals) {
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
                HdArnoldInsertPrimvar(primvars, primvarDesc.name, primvarDesc.role, primvarDesc.interpolation, {}, {});
#else
                HdArnoldInsertPrimvar(primvars, primvarDesc.name, primvarDesc.role, primvarDesc.interpolation, {});
#endif
            } else {
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
                if (primvarDesc.interpolation == HdInterpolationFaceVarying) {
                    VtIntArray valueIndices;
                    const auto value = delegate->GetIndexedPrimvar(id, primvarDesc.name, &valueIndices);
                    HdArnoldInsertPrimvar(
                        primvars, primvarDesc.name, primvarDesc.role, primvarDesc.interpolation, value, valueIndices);
                } else {
#endif
                    HdArnoldInsertPrimvar(
                        primvars, primvarDesc.name, primvarDesc.role, primvarDesc.interpolation,
                        delegate->Get(id, primvarDesc.name)
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
                            ,
                        {}
#endif
                    );
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
                }
#endif
            }
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
    if (numFaces > 0) {
        auto* shidxs = static_cast<uint8_t*>(AiArrayMap(shidxsArray));
        uint8_t subsetId = 0;
        std::fill(shidxs, shidxs + numFaces, static_cast<uint8_t>(numSubsets));
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
    }
    return shidxsArray;
}

bool HdArnoldDeclare(AtNode* node, const TfToken& name, const TfToken& scope, const TfToken& type)
{
    const AtString nameStr{name.GetText()};
    // If the attribute already exists (either as a node entry parameter
    // or as a user data in the node), then we should not call AiNodeDeclare
    // as it would fail.
    if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), nameStr)) {
        AiNodeResetParameter(node, nameStr);
        return true;
    }

    if (AiNodeLookUpUserParameter(node, nameStr)) {
        AiNodeResetParameter(node, nameStr);
    }
    return AiNodeDeclare(node, nameStr, AtString(TfStringPrintf("%s %s", scope.GetText(), type.GetText()).c_str()));
}

PXR_NAMESPACE_CLOSE_SCOPE
