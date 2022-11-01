// Copyright 2022 Autodesk, Inc.
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

void HdArnoldInstancer::CalculateInstanceMatrices(HdArnoldRenderDelegate* renderDelegate, 
    const SdfPath& prototypeId, std::vector<AtNode *> &instancers)
{
#if PXR_VERSION < 2102
    _SyncPrimvars();
#endif
    
    const auto& id = GetId();

    const auto instanceIndices = GetDelegate()->GetInstanceIndices(id, prototypeId);
    if (instanceIndices.empty()) {
        return;
    }
    HdArnoldSampledMatrixArrayType sampleArray;
    
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

    const float fps = 1.0f / (reinterpret_cast<HdArnoldRenderParam*>(renderDelegate->GetRenderParam())->GetFPS());
    const float fps2 = fps * fps;
    VtValue velValue = GetDelegate()->Get(id, HdTokens->velocities);
    VtVec3fArray velocities;
    if (velValue.IsHolding<VtVec3fArray>()) {
        velocities = velValue.UncheckedGet<VtVec3fArray>();
    }
    
    VtValue accelValue = GetDelegate()->Get(id, HdTokens->accelerations);
    VtVec3fArray accelerations;
    if (accelValue.IsHolding<VtVec3fArray>()) {
        accelerations = accelValue.UncheckedGet<VtVec3fArray>();
    }
    bool hasVelocities = (velocities.size() == numInstances);
    bool hasAccelerations = (accelerations.size() == numInstances);
    bool velBlur = hasAccelerations || hasVelocities;

    // TODO(pal): This resamples the values for all the indices, not only the ones we care about.
    for (auto sample = decltype(numSamples){0}; sample < numSamples; sample += 1) {
        const float t = sampleArray.times[sample];
        const float t2 = t * t;

        sampleArray.values[sample].resize(numInstances);

        GfMatrix4d instancerTransform(1.0);
        if (instancerTransforms.count > 0) {
            instancerTransform = instancerTransforms.Resample(t);
        }
        // For the instancer Transform, we used the proper time.
        // But when velocity blur is used, we will consider the default 0-time
        // for all the attributes below that are per-instance.
        float tInst = velBlur ? 0.f : t;
        VtMatrix4dArray transforms;
        if (_transforms.count > 0) {
            transforms = _transforms.Resample(tInst);
        }
        VtVec3fArray translates;
        if (_translates.count > 0) {
            translates = _translates.Resample(tInst);
        }
#if PXR_VERSION >= 2008
        VtQuathArray rotates;
#else
        VtVec4fArray rotates;
#endif
        if (_rotates.count > 0) {
            rotates = _rotates.Resample(tInst);
        }
        VtVec3fArray scales;
        if (_scales.count > 0) {
            scales = _scales.Resample(tInst);
        }

        for (auto instance = decltype(numInstances){0}; instance < numInstances; instance += 1) {
            const auto instanceIndex = instanceIndices[instance];
            auto matrix = instancerTransform;
            if (translates.size() > static_cast<size_t>(instanceIndex)) {
                GfMatrix4d m(1.0);
                GfVec3f translate = translates[instanceIndex];
                // For velocity blur, we add the velocity and/or acceleration 
                // to the current position
                if (hasVelocities) {
                    translate += velocities[instanceIndex] * fps * t;
                }
                if (hasAccelerations) {
                    translate += accelerations[instanceIndex] * fps2 * t2 * 0.5f;
                }
                m.SetTranslate(translate);
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

    std::stringstream ss;
    ss << prototypeId << "_instancer";
    AtNode *instancerNode = AiNode(renderDelegate->GetUniverse(), str::instancer, AtString(ss.str().c_str()));
    instancers.push_back(instancerNode);

    AiNodeDeclare(instancerNode, str::instance_inherit_xform, "constant array BOOL");
    AiNodeSetArray(instancerNode, str::instance_inherit_xform, AiArray(1, 1, AI_TYPE_BOOLEAN, true));

    if (sampleArray.count == 0 || sampleArray.values.front().empty()) {
        AiNodeResetParameter(instancerNode, str::instance_matrix);
        AiNodeResetParameter(instancerNode, str::node_idxs);
        AiNodeResetParameter(instancerNode, str::instance_visibility);
    } else {
        const auto sampleCount = sampleArray.count;
        const auto instanceCount = sampleArray.values.front().size();
        
        auto* matrixArray = AiArrayAllocate(instanceCount, sampleCount, AI_TYPE_MATRIX);
        auto* nodeIdxsArray = AiArrayAllocate(instanceCount, sampleCount, AI_TYPE_UINT);
        auto* matrices = static_cast<AtMatrix*>(AiArrayMap(matrixArray));
        auto* nodeIdxs = static_cast<uint32_t*>(AiArrayMap(nodeIdxsArray));
        std::fill(nodeIdxs, nodeIdxs + instanceCount, 0);
        AiArrayUnmap(nodeIdxsArray);
        auto convertMatrices = [&](size_t sample) {
            std::transform(
                sampleArray.values[sample].begin(), sampleArray.values[sample].end(),
                matrices + sample * instanceCount,
                [](const GfMatrix4d& in) -> AtMatrix { return HdArnoldConvertMatrix(in); });
        };
        convertMatrices(0);
        for (auto sample = decltype(sampleCount){1}; sample < sampleCount; sample += 1) {
            // We check if there is enough data to do the conversion, otherwise we are reusing the first sample.
            if (ARCH_UNLIKELY(sampleArray.values[sample].size() != instanceCount)) {
                std::copy(matrices, matrices + instanceCount, matrices + sample * instanceCount);
            } else {
                convertMatrices(sample);
            }
        }
        auto setMotionParam = [&](const char* name, float value) {
            if (AiNodeLookUpUserParameter(instancerNode, AtString(name)) == nullptr) {
                AiNodeDeclare(instancerNode, AtString(name), str::constantArrayFloat);
            }
            AiNodeSetArray(instancerNode, AtString(name), AiArray(1, 1, AI_TYPE_FLOAT, value));
        };
        if (sampleCount > 1) {
            setMotionParam(str::instance_motion_start, sampleArray.times.front());
            setMotionParam(str::instance_motion_end, sampleArray.times[sampleCount - 1]);
        } else {
            setMotionParam(str::instance_motion_start, 0.0f);
            setMotionParam(str::instance_motion_end, 1.0f);
        }
        AiArrayUnmap(matrixArray);
        AiNodeSetArray(instancerNode, str::instance_matrix, matrixArray);
        AiNodeSetArray(instancerNode, str::node_idxs, nodeIdxsArray);
        SetPrimvars(instancerNode, prototypeId, instanceCount);
    }

    const auto parentId = GetParentId();
    if (parentId.IsEmpty()) {
        return;
    }
    auto* parentInstancer = dynamic_cast<HdArnoldInstancer*>(GetDelegate()->GetRenderIndex().GetInstancer(parentId));
    if (ARCH_UNLIKELY(parentInstancer == nullptr)) {
        return;
    }
    parentInstancer->CalculateInstanceMatrices(renderDelegate, id, instancers);
    AiNodeSetByte(instancerNode, str::visibility, 0);
}


void HdArnoldInstancer::SetPrimvars(AtNode* node, const SdfPath& prototypeId, size_t totalInstanceCount)
{

    VtIntArray instanceIndices = GetDelegate()->GetInstanceIndices(GetId(), prototypeId);
    size_t instanceCount = instanceIndices.size();

    if (instanceCount == 0 || instanceCount != totalInstanceCount)
        return;
        
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
        const char* paramName = primvar.first.GetText();
        
        // We don't need to call NeedsUpdate here, as this function is called once per Prototype, not
        // once per instancer.        

        // For arnold primvars, we want to remove the arnold: prefix in the primvar name. This way, 
        // primvars:arnold:matte will end up as instance_matte in the arnold instancer, which is supported.
       
        auto charStartsWithToken = [&](const char *c, const TfToken &t) { return strncmp(c, t.GetText(), t.size()) == 0; };
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
        HdArnoldSetInstancePrimvar(node, TfToken(paramName), desc.role, instanceIndices, desc.value);
    }
    // Compose the ray flags and get a single AtByte value for each instance. Then make it a single array VtValue
    // and provide it to HdArnoldSetInstancePrimvar
    auto getRayInstanceValue = [&](std::vector<HdArnoldRayFlags> &rayFlags, const TfToken &attrName, AtNode *node,
                VtIntArray &instanceIndices) {
        if (rayFlags.empty())
            return false;

        VtUCharArray valueArray;
        valueArray.reserve(rayFlags.size());
        for (auto &rayFlag : rayFlags) {
            valueArray.push_back(rayFlag.Compose());
        }
        HdArnoldSetInstancePrimvar(node, attrName, HdPrimvarRoleTokens->none, instanceIndices, 
            VtValue(valueArray));
        return true;    
    };
     
    getRayInstanceValue(visibilityFlags, str::t_visibility, node, instanceIndices);
    getRayInstanceValue(sidednessFlags, str::t_sidedness, node, instanceIndices);
    getRayInstanceValue(autobumpVisibilityFlags, str::t_autobump_visibility, node, instanceIndices);

}

PXR_NAMESPACE_CLOSE_SCOPE
