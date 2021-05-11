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
#include "polymesh_adapter.h"

#include <pxr/base/tf/type.h>

#include <pxr/imaging/hd/tokens.h>

#include <constant_strings.h>

PXR_NAMESPACE_OPEN_SCOPE

DEFINE_SHARED_ADAPTER_FACTORY(ImagingArnoldPolymeshAdapter)

bool ImagingArnoldPolymeshAdapter::IsSupported(ImagingArnoldDelegateProxy* proxy) const
{
    return proxy->IsRprimSupported(HdPrimTypeTokens->mesh);
}

void ImagingArnoldPolymeshAdapter::Populate(AtNode* node, ImagingArnoldDelegateProxy* proxy, const SdfPath& id)
{
    proxy->InsertRprim(HdPrimTypeTokens->mesh, id);
}

HdMeshTopology ImagingArnoldPolymeshAdapter::GetMeshTopology(const AtNode* node) const
{
    auto* nsidesArray = AiNodeGetArray(node, str::nsides);
    auto* vidxsArray = AiNodeGetArray(node, str::vidxs);
    if (nsidesArray == nullptr || vidxsArray == nullptr) {
        return {};
    }
    const auto numNsides = AiArrayGetNumElements(nsidesArray);
    const auto numVidxs = AiArrayGetNumElements(vidxsArray);
    if (numNsides == 0 || numVidxs == 0) {
        return {};
    }

    const auto* nsides = static_cast<const uint32_t*>(AiArrayMap(nsidesArray));
    const auto* vidxs = static_cast<const uint32_t*>(AiArrayMap(vidxsArray));
    VtIntArray faceVertexCounts;
    faceVertexCounts.resize(numNsides);
    VtIntArray faceVertexIndices(numVidxs);
    faceVertexIndices.resize(numVidxs);
    std::copy(nsides, nsides + numNsides, faceVertexCounts.begin());
    std::copy(vidxs, vidxs + numVidxs, faceVertexIndices.begin());
    AiArrayUnmap(nsidesArray);
    AiArrayUnmap(vidxsArray);

    HdMeshTopology topology(HdTokens->catmullRom, HdTokens->rightHanded, faceVertexCounts, faceVertexIndices);
    return topology;
}

HdPrimvarDescriptorVector ImagingArnoldPolymeshAdapter::GetPrimvarDescriptors(
    const AtNode* node, HdInterpolation interpolation) const
{
    if (interpolation == HdInterpolationConstant) {
        // TODO(pal): move this to a utility function
        // Check if the displayColor user attribute exists and return it as "color".
        auto* displayColor = AiNodeLookUpUserParameter(node, str::displayColor);
        if (displayColor != nullptr && AiUserParamGetType(displayColor) == AI_TYPE_RGB) {
            return {{str::t_displayColor, HdInterpolationConstant, HdPrimvarRoleTokens->color}};
        }
    } else if (interpolation == HdInterpolationVertex) {
        return {{str::t_points, HdInterpolationVertex, HdPrimvarRoleTokens->point}};
    }
    return {};
}

VtValue ImagingArnoldPolymeshAdapter::Get(const AtNode* node, const TfToken& key) const
{
    if (key == HdTokens->points) {
        auto* vlistArray = AiNodeGetArray(node, str::vlist);
        const auto numElements = AiArrayGetNumElements(vlistArray);
        if (vlistArray == nullptr || numElements < 1 || AiArrayGetNumKeys(vlistArray) < 1) {
            return {};
        }
        // GfVec3f and AtVector has the same memory layout.
        const auto* vlist = static_cast<const GfVec3f*>(AiArrayMap(vlistArray));
        VtVec3fArray points;
        points.assign(vlist, vlist + numElements);
        AiArrayUnmap(vlistArray);
        return VtValue{points};
    } else if (key == str::t_displayColor) {
        const auto displayColor = AiNodeGetRGB(node, str::displayColor);
        return VtValue{GfVec3f{displayColor.r, displayColor.g, displayColor.b}};
    }
    return {};
}

PXR_NAMESPACE_CLOSE_SCOPE
