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
#include "hdarnold.h"

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

const std::array<HdInterpolation, HdInterpolationCount> interpolations{
    HdInterpolationConstant, HdInterpolationUniform,     HdInterpolationVarying,
    HdInterpolationVertex,   HdInterpolationFaceVarying, HdInterpolationInstance,
};

inline bool _Declare(AtNode* node, const TfToken& name, const TfToken& scope, const TfToken& type)
{
    if (AiNodeLookUpUserParameter(node, name.GetText()) != nullptr) {
        AiNodeResetParameter(node, name.GetText());
    }
    return AiNodeDeclare(node, name.GetText(), TfStringPrintf("%s %s", scope.GetText(), type.GetText()).c_str());
}

template <typename T>
inline uint32_t _ConvertArray(AtNode* node, const AtString& name, uint8_t arnoldType, const VtValue& value)
{
    if (!value.IsHolding<T>()) {
        return 0;
    }
    const auto& v = value.UncheckedGet<T>();
    auto* arr = AiArrayConvert(v.size(), 1, arnoldType, v.data());
    AiNodeSetArray(node, name, arr);
    return AiArrayGetNumElements(arr);
}

template <typename T>
inline uint32_t _DeclareAndConvertArray(
    AtNode* node, const TfToken& name, const TfToken& scope, const TfToken& type, uint8_t arnoldType,
    const VtValue& value)
{
    if (!_Declare(node, name, scope, type)) {
        return 0;
    }
    const auto& v = value.UncheckedGet<T>();
    auto* arr = AiArrayConvert(v.size(), 1, arnoldType, v.data());
    AiNodeSetArray(node, name.GetText(), arr);
    return AiArrayGetNumElements(arr);
}

// This is useful for uniform, vertex and face-varying. We need to know the size
// to generate the indices for faceVarying data.
inline uint32_t _DeclareAndAssignFromArray(
    AtNode* node, const TfToken& name, const TfToken& scope, const VtValue& value, bool isColor = false)
{
    if (value.IsHolding<VtBoolArray>()) {
        return _DeclareAndConvertArray<VtBoolArray>(node, name, scope, _tokens->BOOL, AI_TYPE_BOOLEAN, value);
    } else if (value.IsHolding<VtUCharArray>()) {
        return _DeclareAndConvertArray<VtUCharArray>(node, name, scope, _tokens->BYTE, AI_TYPE_BYTE, value);
    } else if (value.IsHolding<VtUIntArray>()) {
        return _DeclareAndConvertArray<VtUIntArray>(node, name, scope, _tokens->UINT, AI_TYPE_UINT, value);
    } else if (value.IsHolding<VtIntArray>()) {
        return _DeclareAndConvertArray<VtIntArray>(node, name, scope, _tokens->INT, AI_TYPE_INT, value);
    } else if (value.IsHolding<VtFloatArray>()) {
        return _DeclareAndConvertArray<VtFloatArray>(node, name, scope, _tokens->FLOAT, AI_TYPE_FLOAT, value);
    } else if (value.IsHolding<VtDoubleArray>()) {
        // TODO
    } else if (value.IsHolding<VtVec2fArray>()) {
        return _DeclareAndConvertArray<VtVec2fArray>(node, name, scope, _tokens->VECTOR2, AI_TYPE_VECTOR2, value);
    } else if (value.IsHolding<VtVec3fArray>()) {
        if (isColor) {
            return _DeclareAndConvertArray<VtVec3fArray>(node, name, scope, _tokens->RGB, AI_TYPE_RGB, value);
        } else {
            return _DeclareAndConvertArray<VtVec3fArray>(node, name, scope, _tokens->VECTOR, AI_TYPE_VECTOR, value);
        }
    } else if (value.IsHolding<VtVec4fArray>()) {
        return _DeclareAndConvertArray<VtVec4fArray>(node, name, scope, _tokens->RGBA, AI_TYPE_RGBA, value);
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
        const auto& v = value.UncheckedGet<GfVec2f>();
        AiNodeSetVec2(node, name.GetText(), v[0], v[1]);
    } else if (value.IsHolding<GfVec3f>()) {
        if (isColor) {
            if (!declareConstant(_tokens->RGB)) {
                return;
            }
            const auto& v = value.UncheckedGet<GfVec3f>();
            AiNodeSetRGB(node, name.GetText(), v[0], v[1], v[2]);
        } else {
            if (!declareConstant(_tokens->VECTOR)) {
                return;
            }
            const auto& v = value.UncheckedGet<GfVec3f>();
            AiNodeSetVec(node, name.GetText(), v[0], v[1], v[2]);
        }
    } else if (value.IsHolding<GfVec4f>()) {
        if (!declareConstant(_tokens->RGBA)) {
            return;
        }
        const auto& v = value.UncheckedGet<GfVec4f>();
        AiNodeSetRGBA(node, name.GetText(), v[0], v[1], v[2], v[3]);
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
        _DeclareAndAssignFromArray(node, name, _tokens->constantArray, value, isColor);
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
                _ConvertArray<VtIntArray>(node, paramName, AI_TYPE_INT, value);
                break;
            case AI_TYPE_UINT:
                _ConvertArray<VtUIntArray>(node, paramName, arrayType, value);
                break;
            case AI_TYPE_BOOLEAN:
                _ConvertArray<VtBoolArray>(node, paramName, arrayType, value);
                break;
            case AI_TYPE_FLOAT:
            case AI_TYPE_HALF:
                _ConvertArray<VtFloatArray>(node, paramName, AI_TYPE_FLOAT, value);
                break;
            case AI_TYPE_VECTOR2:
                _ConvertArray<VtVec2fArray>(node, paramName, arrayType, value);
                break;
            case AI_TYPE_RGB:
            case AI_TYPE_VECTOR:
                _ConvertArray<VtVec3fArray>(node, paramName, arrayType, value);
                break;
            case AI_TYPE_RGBA:
                _ConvertArray<VtVec4fArray>(node, paramName, arrayType, value);
                break;
            default:
                AiMsgError("Unsupported array parameter %s.%s", AiNodeGetName(node), AiParamGetName(pentry).c_str());
        }
        return;
    }
    switch (paramType) {
        case AI_TYPE_BYTE:
            if (value.IsHolding<int>()) {
                AiNodeSetByte(node, paramName, static_cast<uint8_t>(value.UncheckedGet<int>()));
            }
            break;
        case AI_TYPE_INT:
            if (value.IsHolding<int>()) {
                AiNodeSetInt(node, paramName, value.UncheckedGet<int>());
            } else if (value.IsHolding<long>()) {
                AiNodeSetInt(node, paramName, value.UncheckedGet<long>());
            }
            break;
        case AI_TYPE_UINT:
        case AI_TYPE_USHORT:
            if (value.IsHolding<unsigned int>()) {
                AiNodeSetUInt(node, paramName, value.UncheckedGet<unsigned int>());
            }
            break;
        case AI_TYPE_BOOLEAN:
            if (value.IsHolding<bool>()) {
                AiNodeSetBool(node, paramName, value.UncheckedGet<bool>());
            } else if (value.IsHolding<int>()) {
                AiNodeSetBool(node, paramName, value.UncheckedGet<int>() != 0);
            } else if (value.IsHolding<long>()) {
                AiNodeSetBool(node, paramName, value.UncheckedGet<long>() != 0);
            }
            break;
        case AI_TYPE_FLOAT:
        case AI_TYPE_HALF:
            if (value.IsHolding<float>()) {
                AiNodeSetFlt(node, paramName, value.UncheckedGet<float>());
            } else if (value.IsHolding<double>()) {
                AiNodeSetFlt(node, paramName, static_cast<float>(value.UncheckedGet<double>()));
            }
            break;
        case AI_TYPE_RGB:
            if (value.IsHolding<GfVec3f>()) {
                const auto& v = value.UncheckedGet<GfVec3f>();
                AiNodeSetRGB(node, paramName, v[0], v[1], v[2]);
            }
            break;
        case AI_TYPE_RGBA:
            if (value.IsHolding<GfVec4f>()) {
                const auto& v = value.UncheckedGet<GfVec4f>();
                AiNodeSetRGBA(node, paramName, v[0], v[1], v[2], v[3]);
            }
            break;
        case AI_TYPE_VECTOR:
            if (value.IsHolding<GfVec3f>()) {
                const auto& v = value.UncheckedGet<GfVec3f>();
                AiNodeSetVec(node, paramName, v[0], v[1], v[2]);
            }
            break;
        case AI_TYPE_VECTOR2:
            if (value.IsHolding<GfVec2f>()) {
                const auto& v = value.UncheckedGet<GfVec2f>();
                AiNodeSetVec2(node, paramName, v[0], v[1]);
            }
            break;
        case AI_TYPE_STRING:
            if (value.IsHolding<TfToken>()) {
                AiNodeSetStr(node, paramName, value.UncheckedGet<TfToken>().GetText());
            } else if (value.IsHolding<SdfAssetPath>()) {
                const auto& assetPath = value.UncheckedGet<SdfAssetPath>();
                AiNodeSetStr(
                    node, paramName,
                    assetPath.GetResolvedPath().empty() ? assetPath.GetAssetPath().c_str()
                                                        : assetPath.GetResolvedPath().c_str());
            } else if (value.IsHolding<std::string>()) {
                AiNodeSetStr(node, paramName, value.UncheckedGet<std::string>().c_str());
            }
            break;
        case AI_TYPE_POINTER:
        case AI_TYPE_NODE:
            break; // TODO: Should be in the relationships list.
        case AI_TYPE_MATRIX:
            break; // TODO
        case AI_TYPE_ENUM:
            if (value.IsHolding<int>()) {
                AiNodeSetInt(node, paramName, value.UncheckedGet<int>());
            } else if (value.IsHolding<long>()) {
                AiNodeSetInt(node, paramName, value.UncheckedGet<long>());
            } else if (value.IsHolding<TfToken>()) {
                AiNodeSetStr(node, paramName, value.UncheckedGet<TfToken>().GetText());
            } else if (value.IsHolding<std::string>()) {
                AiNodeSetStr(node, paramName, value.UncheckedGet<std::string>().c_str());
            }
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
