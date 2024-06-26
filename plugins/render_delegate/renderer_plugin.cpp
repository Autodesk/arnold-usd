//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2019 Luma Pictures
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
//
// Modifications Copyright 2022 Autodesk, Inc.
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
#include "renderer_plugin.h"

#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include "render_delegate.h"

#include <constant_strings.h>
#include <pxr/base/tf/getEnv.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
     ((houdini_renderer, "houdini:renderer")));
// clang-format on

// Register the Ai plugin with the renderer plugin system.
TF_REGISTRY_FUNCTION(TfType) { HdRendererPluginRegistry::Define<HdArnoldRendererPlugin>(); }

HdRenderDelegate* HdArnoldRendererPlugin::CreateRenderDelegate() { return new HdArnoldRenderDelegate(false, str::t_hydra, nullptr); }

HdRenderDelegate* HdArnoldRendererPlugin::CreateRenderDelegate(const HdRenderSettingsMap& settingsMap)
{
    TfToken context = str::t_hydra;
    bool isBatch = false;
    const auto* houdiniRenderer = TfMapLookupPtr(settingsMap, _tokens->houdini_renderer);
    if (houdiniRenderer != nullptr &&
        ((houdiniRenderer->IsHolding<TfToken>() && houdiniRenderer->UncheckedGet<TfToken>() == str::t_husk) ||
         (houdiniRenderer->IsHolding<std::string>() &&
          houdiniRenderer->UncheckedGet<std::string>() == str::t_husk.GetString()))) {
        context = str::t_husk;
        isBatch = true;
    }
    if (TfGetenv("ARNOLD_FORCE_ABORT_ON_LICENSE_FAIL", "0") != "0" && !AiLicenseIsAvailable()) {
        AiMsgError("Arborting: ARNOLD_FORCE_ABORT_ON_LICENSE_FAIL is set and Arnold license not found");
        return nullptr;
    }
    auto* delegate = new HdArnoldRenderDelegate(isBatch, context, nullptr);
    for (const auto& setting : settingsMap) {
        delegate->SetRenderSetting(setting.first, setting.second);
    }
    return delegate;
}

void HdArnoldRendererPlugin::DeleteRenderDelegate(HdRenderDelegate* renderDelegate) { delete renderDelegate; }

#ifdef USD_HAS_RENDERER_PLUGIN_GPU_ENABLE_PARAM
bool HdArnoldRendererPlugin::IsSupported(bool /*gpuEnabled*/) const { return true; }
#else
bool HdArnoldRendererPlugin::IsSupported() const { return true; }
#endif
PXR_NAMESPACE_CLOSE_SCOPE
