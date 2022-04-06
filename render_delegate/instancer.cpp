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
#include <constant_strings.h>

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

template <typename T1, typename T2>
void _AccumulateSampleTimes(const HdArnoldSampledType<T1>& in, HdArnoldSampledType<T2>& out)
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
#ifdef USD_HAS_SAMPLE_INDEXED_PRIMVAR
                HdArnoldInsertPrimvar(
                    _primvars, primvar.name, primvar.role, primvar.interpolation, GetDelegate()->Get(id, primvar.name),
                    {});
#else
                HdArnoldInsertPrimvar(
                    _primvars, primvar.name, primvar.role, primvar.interpolation, GetDelegate()->Get(id, primvar.name));
#endif
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
            if (translates.size() > static_cast<size_t>(instanceIndex)) {
                GfMatrix4d m(1.0);
                m.SetTranslate(translates[instanceIndex]);
                matrix = m * matrix;
            }
            if (rotates.size() > static_cast<size_t>(instanceIndex)) {
                GfMatrix4d m(1.0);
#if PXR_VERSION >= 2008
                m.SetRotate(GfRotation{rotates[instanceIndex]});
#else
                const auto quat = rotates[instanceIndex];
                m.SetRotate(GfRotation(GfQuaternion(quat[0], GfVec3f(quat[1], quat[2], quat[3]))));
#endif
                matrix = m * matrix;
            }
            if (scales.size() > static_cast<size_t>(instanceIndex)) {
                GfMatrix4d m(1.0);
                m.SetScale(scales[instanceIndex]);
                matrix = m * matrix;
            }
            if (transforms.size() > static_cast<size_t>(instanceIndex)) {
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


void HdArnoldInstancer::SetPrimvars(AtNode* node, const SdfPath& prototypeId, size_t totalInstanceCount, 
    size_t childInstanceCount, size_t &parentInstanceCount)
{
    VtIntArray instanceIndices = GetDelegate()->GetInstanceIndices(GetId(), prototypeId);
    size_t instanceCount = instanceIndices.size();
    if (instanceCount == 0)
        return;
    
    // Recursively call SetPrimvars on eventual instance parents (for nested instancers).
    // Provide the amount of child instances, including this current instancers as it will affect the parent primvars indices.
    // The function will return the accumulated amount of parent instances in parentInstanceCount. We need this multiplier
    // when we set primvars for the current instancer
    const auto parentId = GetParentId();
    if (!parentId.IsEmpty()) {
        // We have a parent instancer, get a pointer to its HdArnoldInstancer class
        auto* parentInstancer = dynamic_cast<HdArnoldInstancer*>(GetDelegate()->GetRenderIndex().GetInstancer(parentId));
        if (parentInstancer) {
            auto id = GetId();
            parentInstancer->SetPrimvars(node, id, totalInstanceCount, childInstanceCount * instanceCount, parentInstanceCount);
        }
    }
    // Verify that the totalInstanceCount we received is consistent with the computed amount of instances, including parent and child ones.
    if (instanceCount * childInstanceCount * parentInstanceCount != totalInstanceCount) {
        return;
    }
    
    // We can receive primvars that have visibility components (e.g. visibility:camera, sidedness:reflection, etc...)
    // In that case we need to concatenate all the component values before we compose them into a single 
    // AtByte visibility. Since each instance can have different data, we need to store a HdArnoldRayFlags for
    // each instance
    std::vector<HdArnoldRayFlags> visibilityFlags;
    std::vector<HdArnoldRayFlags> sidednessFlags;
    std::vector<HdArnoldRayFlags> autobumpVisibilityFlags;

    // Loop over this instancer primvars
    for (auto& primvar : _primvars) {
        auto& desc = primvar.second;
        // We don't need to call NeedsUpdate here, as this function is called once per Prototype, not
        // once per instancer.        

        // For arnold primvars, we want to remove the arnold: prefix in the primvar name. This way, 
        // primvars:arnold:matte will end up as instance_matte in the arnold instancer, which is supported.
        
        auto charStartsWithToken = [&](const char *c, const TfToken &t) { return strncmp(c, t.GetText(), t.size()) == 0; };
        const char* paramName = primvar.first.GetText();

        if (charStartsWithToken(paramName, str::t_arnold_prefix)) {
            // extract the arnold prefix from the primvar name
            paramName = primvar.first.GetText() + str::t_arnold_prefix.size();    
    
            // Apply each component value to the corresponding ray flag
            auto applyRayFlags = [&](const char *primvar, const TfToken& prefix, const VtValue &value, std::vector<HdArnoldRayFlags> &rayFlags) {
                // check if the primvar name starts with the provided prefix
                if (!charStartsWithToken(primvar, prefix))
                    return false;

                // Store a default HdArnoldRayFalgs, with the proper values
                HdArnoldRayFlags defaultFlags;
                defaultFlags.SetHydraFlag(AI_RAY_ALL);
               
                if (value.IsHolding<VtBoolArray>()) {
                    const VtBoolArray &array = value.UncheckedGet<VtBoolArray>();
                    if (array.size() > rayFlags.size()) {                        
                        rayFlags.resize(array.size(), defaultFlags);
                    }
                    // extract the attribute namespace, to get the ray type component (camera, etc...)
                    const auto* rayName = primvar + prefix.size();                    
                    for (size_t i = 0; i < array.size(); ++i) {
                        // apply the ray flag for each instance
                        rayFlags[i].SetRayFlag(rayName, VtValue(array[i]));
                    }
                }
                return true;
            };

            if (applyRayFlags(paramName, str::t_visibility_prefix, desc.value, visibilityFlags))
                continue;
            if (applyRayFlags(paramName, str::t_sidedness_prefix, desc.value, sidednessFlags))
                continue;
            if (applyRayFlags(paramName, str::t_autobump_visibility_prefix, desc.value, autobumpVisibilityFlags))
                continue;
            
        }
        HdArnoldSetInstancePrimvar(node, TfToken(paramName), desc.role, instanceIndices, desc.value, parentInstanceCount, childInstanceCount);
    }

    // Compose the ray flags and get a single AtByte value for each instance. Then make it a single array VtValue
    // and provide it to HdArnoldSetInstancePrimvar
    auto getRayInstanceValue = [&](std::vector<HdArnoldRayFlags> &rayFlags, const TfToken &attrName, AtNode *node,
                VtIntArray &instanceIndices, size_t &parentInstanceCount, size_t &childInstanceCount) {
        if (rayFlags.empty())
            return false;

        VtUCharArray valueArray;
        valueArray.reserve(rayFlags.size());
        for (auto &rayFlag : rayFlags) {
            valueArray.push_back(rayFlag.Compose());
        }
        HdArnoldSetInstancePrimvar(node, attrName, HdPrimvarRoleTokens->none, instanceIndices, 
            VtValue(valueArray), parentInstanceCount, childInstanceCount);
        return true;    
    };
     
    getRayInstanceValue(visibilityFlags, str::t_visibility, node, instanceIndices, parentInstanceCount, childInstanceCount);
    getRayInstanceValue(sidednessFlags, str::t_sidedness, node, instanceIndices, parentInstanceCount, childInstanceCount);
    getRayInstanceValue(autobumpVisibilityFlags, str::t_autobump_visibility, node, instanceIndices, parentInstanceCount, childInstanceCount);

    // We multiply parentInstanceCount by our current instances, so that the caller can take it into account
    parentInstanceCount *= instanceCount;
}

PXR_NAMESPACE_CLOSE_SCOPE
