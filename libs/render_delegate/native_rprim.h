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
/// @file native_rprim.h
///
/// Utilities for managing generic Hydra RPrim for handling native Arnold schemas.
#pragma once
#include <pxr/pxr.h>
#include "api.h"

#include <pxr/imaging/hd/rprim.h>

#include "hdarnold.h"
#include "render_delegate.h"
#include "rprim.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdArnoldNativeRprim : public HdArnoldRprim<HdRprim> {
public:
#if PXR_VERSION >= 2102
    HDARNOLD_API
    HdArnoldNativeRprim(HdArnoldRenderDelegate* renderDelegate, const AtString& arnoldType, const SdfPath& id);
#else
    HDARNOLD_API
    HdArnoldNativeRprim(
        HdArnoldRenderDelegate* renderDelegate, const AtString& arnoldType, const SdfPath& id,
        const SdfPath& instancerId);
#endif

    ~HdArnoldNativeRprim() override = default;

    HDARNOLD_API
    void Sync(
        HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
        const TfToken& reprToken) override;

    HDARNOLD_API
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    HDARNOLD_API
#if PXR_VERSION >= 2002
    const TfTokenVector& GetBuiltinPrimvarNames() const override;
#else
    const TfTokenVector& GetBuiltinPrimvarNames() const;
#endif

private:
    /// List of parameters to query from the Hydra Primitive.
    const HdArnoldRenderDelegate::NativeRprimParamList* _paramList = nullptr;
};

PXR_NAMESPACE_CLOSE_SCOPE
