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
#include <shape_utils.h>

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


inline bool _TokenStartsWithToken(const TfToken& t0, const TfToken& t1)
{
    return strncmp(t0.GetText(), t1.GetText(), t1.size()) == 0;
}

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
    VtVec3fArray emptyVelocities;
    VtVec3fArray emptyAccelerations;
    auto primvarIt = primvars->find(HdTokens->velocities);
    const VtVec3fArray& velocities = (primvarIt != primvars->end() && primvarIt->second.value.IsHolding<VtVec3fArray>())
                                         ? primvarIt->second.value.UncheckedGet<VtVec3fArray>()
                                         : emptyVelocities;

    primvarIt = primvars->find(HdTokens->accelerations);
    const VtVec3fArray& accelerations =
        (primvarIt != primvars->end() && primvarIt->second.value.IsHolding<VtVec3fArray>())
            ? primvarIt->second.value.UncheckedGet<VtVec3fArray>()
            : emptyAccelerations;

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
    AtMatrix mtx;
    for (auto i = decltype(xf.count){0}; i < xf.count; ++i) {
        ConvertValue(mtx, xf.values[i]);
        AiArraySetMtx(matrices, i, mtx);
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
    AtMatrix mtx;
    for (auto i = decltype(xf.count){0}; i < xf.count; ++i) {
        ConvertValue(mtx, xf.values[i]);
        AiArraySetMtx(matrices, i, mtx);
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

    const AtString paramName = AiParamGetName(pentry);
    const uint8_t paramType = AiParamGetType(pentry);

    uint8_t arrayType = 0;
    if (paramType == AI_TYPE_ARRAY) {
        auto* defaultParam = AiParamGetDefault(pentry);
        if (defaultParam->ARRAY() == nullptr) {
            return;
        }
        arrayType = AiArrayGetType(defaultParam->ARRAY());
    }

    InputAttribute attr;
    attr.value = value;
    std::string paramNameStr(paramName.c_str());
    TimeSettings time; // dummy time settings    
    ReadAttribute(attr, node, paramNameStr, time, renderDelegate->GetAPIAdapter(), paramType, arrayType);
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

    DeclareAndAssignParameter(node, name, 
         str::t_constant, value, renderDelegate->GetAPIAdapter(), 
         role == HdPrimvarRoleTokens->color);
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

void HdArnoldSetUniformPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, HdArnoldRenderDelegate *renderDelegate)
{
    DeclareAndAssignParameter(node, name, 
         str::t_uniform, value, renderDelegate->GetAPIAdapter(), 
         role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetUniformPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* delegate, const HdPrimvarDescriptor& primvarDesc, HdArnoldRenderDelegate *renderDelegate)
{
    HdArnoldSetUniformPrimvar(node, primvarDesc.name, primvarDesc.role, delegate->Get(id, primvarDesc.name), renderDelegate);
}

void HdArnoldSetVertexPrimvar(AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, HdArnoldRenderDelegate *renderDelegate)
{
    DeclareAndAssignParameter(node, name, 
        str::t_varying, value, renderDelegate->GetAPIAdapter(), 
        role == HdPrimvarRoleTokens->color);
}

void HdArnoldSetVertexPrimvar(
    AtNode* node, const SdfPath& id, HdSceneDelegate* sceneDelegate, const HdPrimvarDescriptor& primvarDesc, HdArnoldRenderDelegate *renderDelegate)
{
    HdArnoldSetVertexPrimvar(node, primvarDesc.name, primvarDesc.role, sceneDelegate->Get(id, primvarDesc.name), renderDelegate);
}

void HdArnoldSetFaceVaryingPrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtValue& value, HdArnoldRenderDelegate *renderDelegate, 
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
    const VtIntArray& valueIndices,
#endif
    const VtIntArray* vertexCounts, const size_t* vertexCountSum)
{
    const uint32_t numElements = DeclareAndAssignParameter(node, name, 
        str::t_indexed, value, renderDelegate->GetAPIAdapter(), 
        role == HdPrimvarRoleTokens->color);

    // 0 means the array can't be extracted from the VtValue.
    // 1 means the array had a single element, and it was set as a constant user data.
    if (numElements <= 1) {
        return;
    }

    auto* indices =
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
        !valueIndices.empty() ? GenerateVertexIdxs(valueIndices, vertexCounts) :
#endif
                              GenerateVertexIdxs(numElements, vertexCounts, vertexCountSum);

    AiNodeSetArray(node, AtString(TfStringPrintf("%sidxs", name.GetText()).c_str()), indices);
}

void HdArnoldSetInstancePrimvar(
    AtNode* node, const TfToken& name, const TfToken& role, const VtIntArray& indices, const VtValue& value, HdArnoldRenderDelegate* renderDelegate)
{
    VtValue instanceValue;
    if (indices.empty() || !FlattenIndexedValue(value, indices, instanceValue))
        instanceValue = value;
    
    DeclareAndAssignParameter(node, TfToken{TfStringPrintf("instance_%s", name.GetText())}, 
         str::t_constantArray, instanceValue, renderDelegate->GetAPIAdapter(), 
         role == HdPrimvarRoleTokens->color);

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

PXR_NAMESPACE_CLOSE_SCOPE
