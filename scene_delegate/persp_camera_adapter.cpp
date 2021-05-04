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
#include "persp_camera_adapter.h"

#include <pxr/base/tf/type.h>

#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

DEFINE_SHARED_ADAPTER_FACTORY(ImagingArnoldPerspCameraAdapter)

bool ImagingArnoldPerspCameraAdapter::IsSupported(ImagingArnoldDelegateProxy* proxy) const
{
    return proxy->IsSprimSupported(HdPrimTypeTokens->camera);
}

void ImagingArnoldPerspCameraAdapter::Populate(AtNode* node, ImagingArnoldDelegateProxy* proxy, const SdfPath& id) const
{
    proxy->InsertSprim(HdPrimTypeTokens->camera, id);
}

PXR_NAMESPACE_CLOSE_SCOPE
