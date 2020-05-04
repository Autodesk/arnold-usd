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

template <typename T>
inline const VtArray<T>& _LookupInstancePrimvar(const HdArnoldInstancer::PrimvarMap& primvars, const TfToken& primvar)
{
    const auto iter = primvars.find(primvar);
    if (iter != primvars.end()) {
        const auto& value = iter->second;
        if (value.IsHolding<VtArray<T>>()) {
            return value.UncheckedGet<VtArray<T>>();
        }
    }

    const static VtArray<T> ret{};
    return ret;
}

} // namespace

HdArnoldInstancer::HdArnoldInstancer(
    HdArnoldRenderDelegate* renderDelegate, HdSceneDelegate* sceneDelegate, const SdfPath& id,
    const SdfPath& parentInstancerId)
    : HdInstancer(sceneDelegate, id, parentInstancerId), _delegate(renderDelegate)
{
}

void HdArnoldInstancer::_SyncPrimvars()
{
    auto& changeTracker = GetDelegate()->GetRenderIndex().GetChangeTracker();
    const auto& id = GetId();

    auto dirtyBits = changeTracker.GetInstancerDirtyBits(id);
    if (!HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, id)) {
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    dirtyBits = changeTracker.GetInstancerDirtyBits(id);

    if (!HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, id)) {
        return;
    }

    auto* delegate = GetDelegate();
    const auto primvars = delegate->GetPrimvarDescriptors(id, HdInterpolationInstance);
    for (const auto& primvar : primvars) {
        if (!HdChangeTracker::IsPrimvarDirty(dirtyBits, id, primvar.name)) {
            continue;
        }
        auto value = delegate->Get(id, primvar.name);
        if (value.IsEmpty()) {
            continue;
        }
        _primvars[primvar.name] = value;
    }

    changeTracker.MarkInstancerClean(id);
}

VtMatrix4dArray HdArnoldInstancer::CalculateInstanceMatrices(const SdfPath& prototypeId)
{
    _SyncPrimvars();

    const auto& id = GetId();

    const auto instanceIndices = GetDelegate()->GetInstanceIndices(id, prototypeId);

    if (instanceIndices.empty()) {
        return {};
    }

    const auto numInstances = instanceIndices.size();
#ifdef USD_HAS_NEW_INSTANCER_TRANSFORM
    const auto instancerTransform = GetDelegate()->GetInstancerTransform(id);
#else
    const auto instancerTransform = GetDelegate()->GetTransform(id);
#endif

    VtMatrix4dArray transforms(numInstances, instancerTransform);

    const auto& translate = _LookupInstancePrimvar<GfVec3f>(_primvars, _tokens->translate);
    if (!translate.empty()) {
        GfMatrix4d translateMatrix(1.0);
        for (auto i = decltype(numInstances){0}; i < numInstances; ++i) {
            translateMatrix.SetTranslate(translate[instanceIndices[i]]);
            transforms[i] = translateMatrix * transforms[i];
        }
    }

    const auto& rotate = _LookupInstancePrimvar<GfVec4f>(_primvars, _tokens->rotate);
    if (!rotate.empty()) {
        GfMatrix4d rotateMatrix(1.0);
        for (auto i = decltype(numInstances){0}; i < numInstances; ++i) {
            const auto quat = rotate[instanceIndices[i]];
            rotateMatrix.SetRotate(GfRotation(GfQuaternion(quat[0], GfVec3f(quat[1], quat[2], quat[3]))));
            transforms[i] = rotateMatrix * transforms[i];
        }
    }

    const auto& scale = _LookupInstancePrimvar<GfVec3f>(_primvars, _tokens->scale);
    if (!scale.empty()) {
        GfMatrix4d scaleMatrix(1.0);
        for (auto i = decltype(numInstances){0}; i < numInstances; ++i) {
            scaleMatrix.SetScale(scale[instanceIndices[i]]);
            transforms[i] = scaleMatrix * transforms[i];
        }
    }

    const auto& instanceTransform = _LookupInstancePrimvar<GfMatrix4d>(_primvars, _tokens->instanceTransform);
    if (!instanceTransform.empty()) {
        for (auto i = decltype(numInstances){0}; i < numInstances; ++i) {
            transforms[i] = instanceTransform[instanceIndices[i]] * transforms[i];
        }
    }

    // TODO(pal): support motion blur.

    if (GetParentId().IsEmpty()) {
        return transforms;
    }

    auto* parentInstancer =
        dynamic_cast<HdArnoldInstancer*>(GetDelegate()->GetRenderIndex().GetInstancer(GetParentId()));
    if (!TF_VERIFY(parentInstancer)) {
        return transforms;
    }

    const auto parentTransforms = parentInstancer->CalculateInstanceMatrices(id);

    const auto numParentInstances = parentTransforms.size();
    if (numParentInstances == 0) {
        return transforms;
    }

    if (numParentInstances > 1) {
        transforms.resize(numInstances * numParentInstances);
    }

    for (auto i = numParentInstances; i > 0; --i) {
        const auto parentId = i - 1;
        for (auto j = decltype(numInstances){0}; j < numInstances; ++j) {
            transforms[j + parentId * numInstances] = transforms[j] * parentTransforms[parentId];
        }
    }

    return transforms;
}

PXR_NAMESPACE_CLOSE_SCOPE
