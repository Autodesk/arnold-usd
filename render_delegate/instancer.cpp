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
#include "instancer.h"

#include <pxr/base/gf/quaternion.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/imaging/hd/sceneDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (instanceTransform)
    (rotate)
    (scale)
    (translate)
);
// clang-format on

namespace {

template <typename IN, typename OUT>
void _AccumulateSampleTimes(const HdArnoldSampledType<IN>& in, HdArnoldSampledType<OUT>& out)
{
    if (in.count > out.count) {
        out.Resize(in.count);
        out.times = in.times;
    }
}

} // namespace

#if PXR_VERSION >= 2102
HdArnoldInstancer::HdArnoldInstancer(
    HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate, const SdfPath& id)
    : HdInstancer(sceneDelegate, id)
{
}
#else
HdArnoldInstancer::HdArnoldInstancer(
    HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate, const SdfPath& id,
    const SdfPath& parentInstancerId)
    : HdInstancer(sceneDelegate, id, parentInstancerId)
{
}
#endif

#if PXR_VERSION >= 2102
void HdArnoldInstancer::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    _UpdateInstancer(sceneDelegate, dirtyBits);

    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, GetId())) {
        _SyncPrimvars(*dirtyBits);
    }
}
#endif

void HdArnoldInstancer::_SyncPrimvars(
#if PXR_VERSION >= 2102
    HdDirtyBits dirtyBits
#endif
)
{
    auto& changeTracker = GetDelegate()->GetRenderIndex().GetChangeTracker();
    const auto& id = GetId();

#if PXR_VERSION < 2102
    auto dirtyBits = changeTracker.GetInstancerDirtyBits(id);
#endif
    if (!HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, id)) {
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    dirtyBits = changeTracker.GetInstancerDirtyBits(id);

    if (HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, id)) {
        for (const auto& primvar : GetDelegate()->GetPrimvarDescriptors(id, HdInterpolationInstance)) {
            if (!HdChangeTracker::IsPrimvarDirty(dirtyBits, id, primvar.name)) {
                continue;
            }
            if (primvar.name == _tokens->instanceTransform) {
                HdArnoldSampledPrimvarType sample;
                GetDelegate()->SamplePrimvar(id, _tokens->instanceTransform, &sample);
                _transforms.UnboxFrom(sample);
            } else if (primvar.name == _tokens->rotate) {
                HdArnoldSampledPrimvarType sample;
                GetDelegate()->SamplePrimvar(id, _tokens->rotate, &sample);
                _rotates.UnboxFrom(sample);
            } else if (primvar.name == _tokens->scale) {
                HdArnoldSampledPrimvarType sample;
                GetDelegate()->SamplePrimvar(id, _tokens->scale, &sample);
                _scales.UnboxFrom(sample);
            } else if (primvar.name == _tokens->translate) {
                HdArnoldSampledPrimvarType sample;
                GetDelegate()->SamplePrimvar(id, _tokens->translate, &sample);
                _translates.UnboxFrom(sample);
            } else {
                HdArnoldInsertPrimvar(
                    _primvars, primvar.name, primvar.role, primvar.interpolation, GetDelegate()->Get(id, primvar.name));
            }
        }
    }

    changeTracker.MarkInstancerClean(id);
}

void HdArnoldInstancer::CalculateInstanceMatrices(
    const SdfPath& prototypeId, HdArnoldSampledMatrixArrayType& sampleArray)
{
#if PXR_VERSION < 2102
    _SyncPrimvars();
#endif
    sampleArray.Resize(0);

    const auto& id = GetId();

    const auto instanceIndices = GetDelegate()->GetInstanceIndices(id, prototypeId);

    if (instanceIndices.empty()) {
        return;
    }

    const auto numInstances = instanceIndices.size();

    HdArnoldSampledType<GfMatrix4d> instancerTransforms;
    GetDelegate()->SampleInstancerTransform(id, &instancerTransforms);

    // Similarly to the HdPrman render delegate, we take a look at the sampled values, and take the one with the
    // most samples and use its time range.
    // TODO(pal): Improve this further by using the widest time range and calculate sample count based on that.
    _AccumulateSampleTimes(instancerTransforms, sampleArray);
    _AccumulateSampleTimes(_transforms, sampleArray);
    _AccumulateSampleTimes(_translates, sampleArray);
    _AccumulateSampleTimes(_rotates, sampleArray);
    _AccumulateSampleTimes(_scales, sampleArray);

    const auto numSamples = sampleArray.count;
    if (numSamples == 0) {
        return;
    }

    // TODO(pal): This resamples the values for all the indices, not only the ones we care about.
    for (auto sample = decltype(numSamples){0}; sample < numSamples; sample += 1) {
        const auto t = sampleArray.times[sample];
        sampleArray.values[sample].resize(numInstances);

        GfMatrix4d instancerTransform(1.0);
        if (instancerTransforms.count > 0) {
            instancerTransform = instancerTransforms.Resample(t);
        }
        VtMatrix4dArray transforms;
        if (_transforms.count > 0) {
            transforms = _transforms.Resample(t);
        }
        VtVec3fArray translates;
        if (_translates.count > 0) {
            translates = _translates.Resample(t);
        }
#if PXR_VERSION >= 2008
        VtQuathArray rotates;
#else
        VtVec4fArray rotates;
#endif
        if (_rotates.count > 0) {
            rotates = _rotates.Resample(t);
        }
        VtVec3fArray scales;
        if (_scales.count > 0) {
            scales = _scales.Resample(t);
        }

        for (auto instance = decltype(numInstances){0}; instance < numInstances; instance += 1) {
            const auto instanceIndex = instanceIndices[instance];
            auto matrix = instancerTransform;
            if (translates.size() > instanceIndex) {
                GfMatrix4d m(1.0);
                m.SetTranslate(translates[instanceIndex]);
                matrix = m * matrix;
            }
            if (rotates.size() > instanceIndex) {
                GfMatrix4d m(1.0);
#if PXR_VERSION >= 2008
                m.SetRotate(GfRotation{rotates[instanceIndex]});
#else
                const auto quat = rotates[instanceIndex];
                m.SetRotate(GfRotation(GfQuaternion(quat[0], GfVec3f(quat[1], quat[2], quat[3]))));
#endif
                matrix = m * matrix;
            }
            if (rotates.size() > instanceIndex) {
                GfMatrix4d m(1.0);
                m.SetScale(scales[instanceIndex]);
                matrix = m * matrix;
            }
            if (transforms.size() > instanceIndex) {
                matrix = transforms[instanceIndex] * matrix;
            }
            sampleArray.values[sample][instance] = matrix;
        }
    }

    const auto parentId = GetParentId();
    if (parentId.IsEmpty()) {
        return;
    }

    auto* parentInstancer = dynamic_cast<HdArnoldInstancer*>(GetDelegate()->GetRenderIndex().GetInstancer(parentId));
    if (ARCH_UNLIKELY(parentInstancer == nullptr)) {
        return;
    }

    HdArnoldSampledMatrixArrayType parentMatrices;
    parentInstancer->CalculateInstanceMatrices(id, parentMatrices);
    if (parentMatrices.count == 0 || parentMatrices.values.front().empty()) {
        return;
    }

    HdArnoldSampledMatrixArrayType childMatrices{sampleArray};
    _AccumulateSampleTimes(parentMatrices, sampleArray);
    for (auto sample = decltype(numSamples){0}; sample < numSamples; sample += 1) {
        const float t = sampleArray.times[sample];

        auto currentParentMatrices = parentMatrices.Resample(t);
        auto currentChildMatrices = childMatrices.Resample(t);

        sampleArray.values[sample].resize(currentParentMatrices.size() * currentChildMatrices.size());
        size_t matrix = 0;
        for (const auto& parentMatrix : currentParentMatrices) {
            for (const auto& childMatrix : currentChildMatrices) {
                sampleArray.values[sample][matrix] = childMatrix * parentMatrix;
                matrix += 1;
            }
        }
    }
}

void HdArnoldInstancer::SetPrimvars(AtNode* node, const SdfPath& prototypeId, size_t instanceCount)
{
    // TODO(pal): Add support for inheriting primvars from parent instancers.
    VtIntArray instanceIndices;
    for (auto& primvar : _primvars) {
        auto& desc = primvar.second;
        if (!desc.NeedsUpdate()) {
            continue;
        }
        if (instanceIndices.empty()) {
            instanceIndices = GetDelegate()->GetInstanceIndices(GetId(), prototypeId);
            if (instanceIndices.empty() || instanceIndices.size() != instanceCount) {
                return;
            }
        }
        HdArnoldSetInstancePrimvar(node, primvar.first, desc.role, instanceIndices, desc.value);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
