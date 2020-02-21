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
#include "points.h"

#include "constant_strings.h"
#include "material.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

template <unsigned AT, typename T>
inline void _SetMotionBlurredPrimvar(
    HdSceneDelegate* delegate, const SdfPath& id, const TfToken& primvarName, AtNode* node, const AtString& paramName)
{
    constexpr size_t maxSamples = 2;
    HdTimeSampleArray<VtValue, maxSamples> xf;
    delegate->SamplePrimvar(id, primvarName, &xf);
    if (xf.count > 0 && ARCH_LIKELY(xf.values[0].IsHolding<VtArray<T>>())) {
        const auto& v0 = xf.values[0].Get<VtArray<T>>();
        if (xf.count > 1 && ARCH_UNLIKELY(!xf.values[1].IsHolding<VtArray<T>>())) {
            xf.count = 1;
        }
        auto* arr = AiArrayAllocate(v0.size(), xf.count, AT);
        AiArraySetKey(arr, 0, v0.data());
        if (xf.count > 1) {
            const auto& v1 = xf.values[1].Get<VtArray<T>>();
            if (ARCH_LIKELY(v1.size() == v0.size())) {
                AiArraySetKey(arr, 1, v1.data());
            } else {
                AiArraySetKey(arr, 1, v0.data());
            }
        }
        AiNodeSetArray(node, paramName, arr);
    }
}

} // namespace

HdArnoldPoints::HdArnoldPoints(HdArnoldRenderDelegate* delegate, const SdfPath& id, const SdfPath& instancerId)
    : HdPoints(id, instancerId), _shape(str::points, delegate, id, GetPrimId())
{
}

HdArnoldPoints::~HdArnoldPoints() {}

HdDirtyBits HdArnoldPoints::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility |
           HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyWidths | HdChangeTracker::DirtyMaterialId |
           HdChangeTracker::DirtyInstanceIndex;
}

void HdArnoldPoints::Sync(
    HdSceneDelegate* delegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    const auto& id = GetId();
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        param->End();
        HdArnoldSetPositionFromPrimvar(_shape.GetShape(), id, delegate, str::points);
        // HdPrman exports points like this, but this method does not support
        // motion blurred points.
    } else if (*dirtyBits & HdChangeTracker::DirtyPoints) {
        param->End();
        const auto pointsValue = delegate->Get(id, HdTokens->points);
        if (!pointsValue.IsEmpty() && pointsValue.IsHolding<VtVec3fArray>()) {
            const auto& points = pointsValue.UncheckedGet<VtVec3fArray>();
            auto* arr = AiArrayAllocate(points.size(), 1, AI_TYPE_VECTOR);
            AiArraySetKey(arr, 0, points.data());
            AiNodeSetArray(_shape.GetShape(), str::points, arr);
        }
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)) {
        param->End();
        HdArnoldSetRadiusFromPrimvar(_shape.GetShape(), id, delegate);
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        param->End();
        _UpdateVisibility(delegate, dirtyBits);
        AiNodeSetByte(_shape.GetShape(), str::visibility, _sharedData.visible ? AI_RAY_ALL : uint8_t{0});
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        param->End();
        const auto* material = reinterpret_cast<const HdArnoldMaterial*>(
            delegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, delegate->GetMaterialId(id)));
        if (material != nullptr) {
            AiNodeSetPtr(_shape.GetShape(), str::shader, material->GetSurfaceShader());
        } else {
            AiNodeSetPtr(_shape.GetShape(), str::shader, _shape.GetDelegate()->GetFallbackShader());
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        param->End();
        for (const auto& primvar : delegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationConstant)) {
            HdArnoldSetConstantPrimvar(_shape.GetShape(), id, delegate, primvar);
        }

        auto convertToUniformPrimvar = [&](const HdPrimvarDescriptor& primvar) {
            if (primvar.name != HdTokens->points && primvar.name != HdTokens->widths) {
                HdArnoldSetUniformPrimvar(_shape.GetShape(), id, delegate, primvar);
            }
        };

        for (const auto& primvar : delegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationUniform)) {
            convertToUniformPrimvar(primvar);
        }

        for (const auto& primvar : delegate->GetPrimvarDescriptors(id, HdInterpolation::HdInterpolationVertex)) {
            // Per vertex attributes are uniform on points.
            convertToUniformPrimvar(primvar);
        }
    }

    _shape.Sync(this, *dirtyBits, delegate, param);

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits HdArnoldPoints::_PropagateDirtyBits(HdDirtyBits bits) const { return bits & HdChangeTracker::AllDirty; }

void HdArnoldPoints::_InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits)
{
    TF_UNUSED(reprToken);
    TF_UNUSED(dirtyBits);
}

PXR_NAMESPACE_CLOSE_SCOPE
