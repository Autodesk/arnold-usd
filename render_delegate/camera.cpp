// Copyright 2020 Autodesk, Inc.
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
#include "camera.h"

#include "constant_strings.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

HdArnoldCamera::HdArnoldCamera(HdArnoldRenderDelegate* delegate, const SdfPath& id) : HdCamera(id)
{
    // We create a persp_camera by default and optionally replace the node in ::Sync.
    _camera = AiNode(delegate->GetUniverse(), str::persp_camera);
    if (!id.IsEmpty()) {
        AiNodeSetStr(_camera, str::name, id.GetText());
    }
}

HdArnoldCamera::~HdArnoldCamera() { AiNodeDestroy(_camera); }

void HdArnoldCamera::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    auto* param = reinterpret_cast<HdArnoldRenderParam*>(renderParam);
    auto oldBits = *dirtyBits;
    HdCamera::Sync(sceneDelegate, renderParam, &oldBits);

    // We can change between perspective and orthographic camera.
    if (*dirtyBits & HdCamera::DirtyProjMatrix) {
        param->Interrupt();
        const auto& projMatrix = GetProjectionMatrix();
        const auto fov = static_cast<float>(GfRadiansToDegrees(atan(1.0 / projMatrix[0][0]) * 2.0));
        AiNodeSetFlt(_camera, str::fov, fov);
    }

    if (*dirtyBits & HdCamera::DirtyViewMatrix) {
        param->Interrupt();
        HdArnoldSetTransform(_camera, sceneDelegate, GetId());
    }

    if (*dirtyBits & HdCamera::DirtyParams) {
        param->Interrupt();
    }

    *dirtyBits = HdChangeTracker::Clean;
}

PXR_NAMESPACE_CLOSE_SCOPE
