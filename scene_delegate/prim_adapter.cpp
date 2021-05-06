// Copyright 2021 Autodesk, Inc.
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
#include "prim_adapter.h"

#include <pxr/base/tf/type.h>

#include <common_utils.h>
#include <constant_strings.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType) { TfType::Define<ImagingArnoldPrimAdapter>(); }

HdMeshTopology ImagingArnoldPrimAdapter::GetMeshTopology(const AtNode* node) const { return {}; }

GfMatrix4d ImagingArnoldPrimAdapter::GetTransform(const AtNode* node) const
{
    auto* matrices = AiNodeGetArray(node, str::matrix);
    if (matrices == nullptr || AiArrayGetNumElements(matrices) < 1) {
        return GfMatrix4d(1);
    }
    return ArnoldUsdConvertMatrix(AiArrayGetMtx(matrices, 0));
}

size_t ImagingArnoldPrimAdapter::SampleTransform(
    const AtNode* node, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues) const
{
    if (Ai_unlikely(sampleTimes == nullptr || sampleValues == nullptr)) {
        return 0;
    }
    auto* matricesArray = AiNodeGetArray(node, str::matrix);
    if (matricesArray == nullptr) {
        return 0;
    }
    const auto numElements = static_cast<size_t>(AiArrayGetNumElements(matricesArray));
    if (numElements == 0) {
        return 0;
    }
    const auto motionStart = AiNodeGetFlt(node, str::motion_start);
    const auto motionEnd = AiNodeGetFlt(node, str::motion_end);
    const auto numSamples = std::min(maxSampleCount, numElements);
    // We need to re-interpolate values.
    if (maxSampleCount < numElements) {
        // TODO(pal): implement
        return 0;
    }
    auto* matrices = static_cast<const AtMatrix*>(AiArrayMap(matricesArray));
    for (auto sample = decltype(numSamples){0}; sample < numSamples; sample += 1) {
        sampleTimes[sample] =
            AiLerp(static_cast<float>(sample) / static_cast<float>(numSamples - 1), motionStart, motionEnd);
        sampleValues[sample] = ArnoldUsdConvertMatrix(matrices[sample]);
    }
    AiArrayUnmap(matricesArray);
    return numSamples;
}

GfRange3d ImagingArnoldPrimAdapter::GetExtent(const AtNode* node) const
{
    return {GfVec3d{-AI_BIG, -AI_BIG, -AI_BIG}, GfVec3d{GfVec3d{AI_BIG, AI_BIG, AI_BIG}}};
}

HdPrimvarDescriptorVector ImagingArnoldPrimAdapter::GetPrimvarDescriptors(
    const AtNode* node, HdInterpolation interpolation) const
{
    return {};
}

VtValue ImagingArnoldPrimAdapter::Get(const AtNode* node, const TfToken& key) const
{
    // Should handle generic fallback.
    return {};
}

PXR_NAMESPACE_CLOSE_SCOPE
