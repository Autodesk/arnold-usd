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

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((houdini_renderer, "houdini:renderer"))
    (batchCommandLine)
    );

// clang-format on

// Register the Ai plugin with the renderer plugin system.
TF_REGISTRY_FUNCTION(TfType) { HdRendererPluginRegistry::Define<HdArnoldRendererPlugin>(); }

HdRenderDelegate* HdArnoldRendererPlugin::CreateRenderDelegate() { return new HdArnoldRenderDelegate(false, str::t_hydra, nullptr, AI_SESSION_INTERACTIVE); }

HdRenderDelegate* HdArnoldRendererPlugin::CreateRenderDelegate(const HdRenderSettingsMap& settingsMap)
{
    TfToken context = str::t_hydra;
    bool isBatch = false;
    AtSessionMode sessionType = AI_SESSION_INTERACTIVE;    
    const auto* houdiniRenderer = TfMapLookupPtr(settingsMap, _tokens->houdini_renderer);
    if (houdiniRenderer != nullptr &&
        ((houdiniRenderer->IsHolding<TfToken>() && houdiniRenderer->UncheckedGet<TfToken>() == str::t_husk) ||
         (houdiniRenderer->IsHolding<std::string>() &&
          houdiniRenderer->UncheckedGet<std::string>() == str::t_husk.GetString()))) {
        context = str::t_husk;
        isBatch = true;
        // We set the session type to batch since we're batch rendering.
        // However, the husk command line can specify an amount of frames with the -n argument, 
        // in which case the session is only created once and each new frame is treated as
        // being interactive changes. Therefore, if the -n argument is used to render multiple frames
        // we roll back to an interactive session
        sessionType = AI_SESSION_BATCH;
        for (const auto& setting : settingsMap) {
            if (setting.first != _tokens->batchCommandLine) 
                continue;
            if (setting.second.IsHolding<VtStringArray>()) {
                const VtStringArray &commandLine = setting.second.UncheckedGet<VtArray<std::string>>();
                for (unsigned int i = 0; i < commandLine.size(); ++i) {
                    if ((commandLine[i] == "-n") 
                            && i < commandLine.size() - 2) {
                        int numFrames = std::atoi(commandLine[++i].c_str());
                        if (numFrames > 1) {
                            // We need an interactive session since we're rendering 
                            // multiple frames in a single render session
                            sessionType = AI_SESSION_INTERACTIVE;                    
                        }
                        break;                    
                    }
                }
            }
        }
    }
    
    auto* delegate = new HdArnoldRenderDelegate(isBatch, context, nullptr, sessionType);
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
