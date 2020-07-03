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
#ifdef _WIN32
#include <FnPlatform/Windows.h>
#endif

#include "material.h"

#include <pxr/base/tf/token.h>

#include <pxr/usd/usdShade/material.h>

#include <usdKatana/utils.h>

#include <FnAttribute/FnAttribute.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (arnold)
);
// clang-format on

namespace {

// TODO(pal): Use some of the macros from KtoA or ship KtoA with the macros
// header.
namespace str {

FnPlatform::StringView arnoldSurface("material.terminals.arnoldSurface");
FnPlatform::StringView arnoldSurfacePort("material.terminals.arnoldSurfacePort");
FnPlatform::StringView arnoldVolume("material.terminals.arnoldVolume");
FnPlatform::StringView arnoldVolumePort("material.terminals.arnoldVolumePort");
FnPlatform::StringView arnoldDisplacement("material.terminals.arnoldDisplacement");
FnPlatform::StringView arnoldDisplacementPort("material.terminals.arnoldDisplacementPort");
FnPlatform::StringView materialNodes("material.nodes");
FnPlatform::StringView type("type");

} // namespace str

} // namespace

void modifyMaterial(
    const PxrUsdKatanaUsdInPrivateData& privateData, FnKat::GroupAttribute opArgs,
    FnKat::GeolibCookInterface& interface)
{
    const auto prim = privateData.GetUsdPrim();

    UsdShadeMaterial material{prim};
    if (!material) {
        return;
    }

    // First, we are going to check if there is a shader connected to any of the arnold terminals (or the default ones).
    // Then we set these shaders to the corresponding Arnold terminals.
    auto computeShader = [&](const UsdShadeShader& shader, const FnPlatform::StringView& terminal,
                             const FnPlatform::StringView& port) {
        if (!shader) {
            return;
        }
        const auto handle = PxrUsdKatanaUtils::GenerateShadingNodeHandle(shader.GetPrim());
        if (handle.empty()) {
            return;
        }
        interface.setAttr(terminal, FnKat::StringAttribute(handle));
        interface.setAttr(port, FnKat::StringAttribute("out"));
    };
    computeShader(material.ComputeSurfaceSource(_tokens->arnold), str::arnoldSurface, str::arnoldSurfacePort);
    computeShader(material.ComputeVolumeSource(_tokens->arnold), str::arnoldVolume, str::arnoldVolumePort);
    computeShader(
        material.ComputeDisplacementSource(_tokens->arnold), str::arnoldDisplacement, str::arnoldDisplacementPort);
    // We also need to iterate through all the nodes, and remove 'arnold:' from the node type.
    FnKat::GroupAttribute nodesAttr = interface.getOutputAttr(str::materialNodes);
    if (!nodesAttr.isValid()) {
        return;
    }
    const auto numberOfChildren = nodesAttr.getNumberOfChildren();
    for (auto child = decltype(numberOfChildren){0}; child < numberOfChildren; child += 1) {
        FnKat::GroupAttribute childGrp = nodesAttr.getChildByIndex(child);
        if (!childGrp.isValid()) {
            continue;
        }
        FnKat::StringAttribute typeAttr = childGrp.getChildByName(str::type);
        if (!typeAttr.isValid()) {
            continue;
        }
        const auto typeStr = typeAttr.getValue("", false);
        if (typeStr.find("arnold:") == 0) {
            interface.setAttr(
                TfStringPrintf("material.nodes.%s.type", nodesAttr.getChildName(child).c_str()),
                FnKat::StringAttribute(typeStr.substr(7)));
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
