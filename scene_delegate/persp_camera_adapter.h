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
/// @file scene_delegate/persp_camera_adapter.h
///
/// Adapter for converting Arnold persp_camera to Hydra camera.
#pragma once
#include "api.h"

#include <pxr/pxr.h>

#include "prim_adapter.h"

PXR_NAMESPACE_OPEN_SCOPE

class ImagingArnoldPerspCameraAdapter : public ImagingArnoldPrimAdapter {
public:
    using BaseAdapter = ImagingArnoldPrimAdapter;

    bool IsSupported(ImagingArnoldDelegateProxy* proxy) const override;

    void Populate(AtNode* node, ImagingArnoldDelegateProxy* proxy, const SdfPath& id) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE
