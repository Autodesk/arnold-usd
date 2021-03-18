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
#include "native_rprim.h"

PXR_NAMESPACE_OPEN_SCOPE

#if PXR_VERSION >= 2102
HdArnoldNativeRprim::HdArnoldNativeRprim(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id) : HdRprim(id) {}
#else
HdArnoldNativeRprim::HdArnoldNativeRprim(
    HdArnoldRenderDelegate* renderDelegate, const SdfPath& id, const SdfPath& instancerId)
    : HdRprim(id, instancerId)
{
}
#endif

void HdArnoldNativeRprim::Sync(HdSceneDelegate* delegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits, const TfToken& reprToken)
{
    TF_UNUSED(delegate);
    TF_UNUSED(renderParam);
    TF_UNUSED(dirtyBits);
    TF_UNUSED(reprToken);
}

HdDirtyBits HdArnoldNativeRprim::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::AllDirty;
}

const TfTokenVector& HdArnoldNativeRprim::GetBuiltinPrimvarNames() const {
    const static TfTokenVector r{};
    return r;
}

HdDirtyBits HdArnoldNativeRprim::_PropagateDirtyBits(HdDirtyBits bits) const {
    return bits & HdChangeTracker::AllDirty;
}

void HdArnoldNativeRprim::_InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) {
    TF_UNUSED(reprToken);
    TF_UNUSED(dirtyBits);
}

PXR_NAMESPACE_CLOSE_SCOPE
