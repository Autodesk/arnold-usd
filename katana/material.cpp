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
#include "material.h"

#include <pxr/base/tf/token.h>

#include <pxr/usd/usdShade/material.h>

#include <usdKatana/utils.h>

#include <FnAttribute/FnAttribute.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
        (arnold)
        (arnoldSurface)
        (arnoldSurfacePort)
        (arnoldVolume)
        (arnoldVolumePort)
        (arnoldDisplacement)
        (arnoldDisplacementPort)
);
// clang-format on

void modifyMaterial(
    const PxrUsdKatanaUsdInPrivateData& privateData, FnKat::GroupAttribute opArgs,
    FnKat::GeolibCookInterface& interface)
{
    const auto prim = privateData.GetUsdPrim();

    UsdShadeMaterial material{prim};
    if (!material) {
        return;
    }

    auto computeShader = [&](const UsdShadeShader& shader, const TfToken& terminal, const TfToken& port) {
        if (!shader) {
            return;
        }
        const auto handle = PxrUsdKatanaUtils::GenerateShadingNodeHandle(shader.GetPrim());
        if (handle.empty()) {
            return;
        }
        interface.setAttr(terminal.GetString(), FnKat::StringAttribute(handle));
        interface.setAttr(port.GetString(), FnKat::StringAttribute("out"));
    };
    computeShader(material.ComputeSurfaceSource(_tokens->arnold), _tokens->arnoldSurface, _tokens->arnoldSurfacePort);
    computeShader(material.ComputeVolumeSource(_tokens->arnold), _tokens->arnoldVolume, _tokens->arnoldVolumePort);
    computeShader(
        material.ComputeDisplacementSource(_tokens->arnold), _tokens->arnoldDisplacement,
        _tokens->arnoldDisplacementPort);
}

PXR_NAMESPACE_CLOSE_SCOPE
