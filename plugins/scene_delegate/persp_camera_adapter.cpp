//
// SPDX-License-Identifier: Apache-2.0
//

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
#include "persp_camera_adapter.h"

#include <pxr/base/tf/type.h>

#include <pxr/base/gf/camera.h>

#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/tokens.h>

#include <constant_strings.h>

PXR_NAMESPACE_OPEN_SCOPE

DEFINE_SHARED_ADAPTER_FACTORY(ImagingArnoldPerspCameraAdapter)

bool ImagingArnoldPerspCameraAdapter::IsSupported(ImagingArnoldDelegateProxy* proxy) const
{
    return proxy->IsSprimSupported(HdPrimTypeTokens->camera);
}

void ImagingArnoldPerspCameraAdapter::Populate(AtNode* node, ImagingArnoldDelegateProxy* proxy, const SdfPath& id)
{
    proxy->InsertSprim(HdPrimTypeTokens->camera, id);
}

VtValue ImagingArnoldPerspCameraAdapter::Get(const AtNode* node, const TfToken& key) const
{
    // Returning usd default values for now.
    if (key == HdCameraTokens->projection) {
        return VtValue{HdCamera::Perspective};
    } else if (key == HdCameraTokens->horizontalAperture) {
        /*const auto fov = AiNodeGetFlt(node, str::fov);
        auto horizontalAperture = tanf(fov * AI_DTOR * 0.5f);
        horizontalAperture *= (2.f * 50.f * static_cast<float>(GfCamera::FOCAL_LENGTH_UNIT));
        horizontalAperture /= static_cast<float>(GfCamera::APERTURE_UNIT);
        return VtValue{horizontalAperture};*/
        return VtValue{20.9550f};
    } else if (key == HdCameraTokens->verticalAperture) {
        return VtValue{15.2908f};
    } else if (key == HdCameraTokens->horizontalApertureOffset) {
        return VtValue{0.0f};
    } else if (key == HdCameraTokens->verticalApertureOffset) {
        return VtValue{0.0f};
    } else if (key == HdCameraTokens->focalLength) {
        return VtValue{50.0f};
    } else if (key == HdCameraTokens->clippingRange) {
        // The default values on the persp_camera are really bad for real-time renderers.
        return VtValue{GfRange1f(AiNodeGetFlt(node, str::near_clip), AiNodeGetFlt(node, str::far_clip))};
    } else if (key == HdCameraTokens->clipPlanes) {
        return {};
    } else if (key == HdCameraTokens->fStop) {
        return VtValue{0.0f};
    } else if (key == HdCameraTokens->focusDistance) {
        return VtValue{AiNodeGetFlt(node, str::focus_distance)};
    } else if (key == HdCameraTokens->shutterOpen) {
        return VtValue{double{AiNodeGetFlt(node, str::shutter_start)}};
    } else if (key == HdCameraTokens->shutterClose) {
        return VtValue{double{AiNodeGetFlt(node, str::shutter_end)}};
    } else if (key == HdCameraTokens->exposure) {
        return VtValue{AiNodeGetFlt(node, str::exposure)};
    }
    return ImagingArnoldPrimAdapter::Get(node, key);
}

PXR_NAMESPACE_CLOSE_SCOPE
