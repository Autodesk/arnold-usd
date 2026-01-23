//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2026 Autodesk, Inc.
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
/// @file render_settings.cpp
///
/// Hydra 2.0 Render Settings Prim for Arnold.

#include "render_settings.h"

#if PXR_VERSION >= 2308

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/staticTokens.h>

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/imaging/hd/sceneIndexPrimView.h>
#include <pxr/imaging/hd/utils.h>
#include <pxr/imaging/hdsi/renderSettingsFilteringSceneIndex.h>

#include <pxr/usdImaging/usdImaging/renderSettingsAdapter.h>

#include "camera.h"
#include "render_delegate.h"
#include "render_param.h"
#include "utils.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

// Debug codes for render settings
TF_DEBUG_CODES(
    HDARNOLD_RENDER_SETTINGS
);

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(HDARNOLD_RENDER_SETTINGS,
        "Debug logging for Arnold render settings prim.");
}

// Environment variable to control whether render settings prim drives the
// render pass. This allows comparison between legacy AOV bindings and
// render settings prim-driven rendering.
TF_DEFINE_ENV_SETTING(
    HDARNOLD_RENDER_SETTINGS_DRIVE_RENDER_PASS,
    false,
    "Drive the render pass using the first RenderProduct on the render "
    "settings prim when the render pass has AOV bindings.");

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    // Render terminal connections (integrators, filters, etc.)
    ((arnoldIntegrator, "arnold:integrator"))
    ((arnoldImagers, "arnold:imagers"))
    // Legacy terminal connections (for backward compatibility)
    ((outputsArnoldIntegrator, "outputs:arnold:integrator"))
    ((outputsArnoldImagers, "outputs:arnold:imagers"))
);

#if PXR_VERSION <= 2308
TF_DEFINE_PRIVATE_TOKENS(
    _legacyTokens,
    ((fallbackPath, "/Render/__HdsiRenderSettingsFilteringSceneIndex__FallbackSettings"))
    ((renderScope, "/Render"))
    (shutterInterval)
);
#endif

namespace {

/// Translates a settings property name to an Arnold option name.
///
/// Strips the "arnold:" prefix from property names.
/// Example: "arnold:AA_samples" -> "AA_samples"
///
/// @param propertyName The property name from the render settings.
/// @return The Arnold option name.
std::string
_GetArnoldOptionName(std::string const& propertyName)
{
    // Strip "arnold:" prefix if present
    if (TfStringStartsWith(propertyName, "arnold:")) {
        return propertyName.substr(7); // strlen("arnold:")
    }

    TF_WARN("Could not translate settings property %s to Arnold option name.",
            propertyName.c_str());
    return propertyName;
}

/// Generates a dictionary of Arnold options from render settings.
///
/// @param settings The render settings dictionary.
/// @return A dictionary of Arnold options.
VtDictionary
_GenerateArnoldOptions(VtDictionary const& settings)
{
    VtDictionary options;

    for (auto const& pair : settings) {
        const std::string& name = pair.first;
        const TfToken tokenName(name);

        // Skip render terminal connections
        if (tokenName == _tokens->arnoldIntegrator ||
            tokenName == _tokens->arnoldImagers ||
            // Legacy terminal connections
            tokenName == _tokens->outputsArnoldIntegrator ||
            tokenName == _tokens->outputsArnoldImagers) {
            continue;
        }

        const std::string arnoldName = _GetArnoldOptionName(name);
        options[arnoldName] = pair.second;
    }

    return options;
}

/// Helper function to multiply and round a float vector by an integer vector.
///
/// @param a The float vector.
/// @param b The integer vector.
/// @return The result as an integer vector.
GfVec2i
_MultiplyAndRound(const GfVec2f& a, const GfVec2i& b)
{
    return GfVec2i(std::roundf(a[0] * b[0]),
                   std::roundf(a[1] * b[1]));
}

/// Checks if there's a non-fallback render settings prim in the scene.
///
/// @param si The scene index.
/// @return True if a non-fallback render settings prim exists.
bool
_HasNonFallbackRenderSettingsPrim(const HdSceneIndexBaseRefPtr& si)
{
    if (!si) {
        return false;
    }

#if PXR_VERSION >= 2311
    const SdfPath& renderScope =
        HdsiRenderSettingsFilteringSceneIndex::GetRenderScope();
    const SdfPath& fallbackPrimPath =
        HdsiRenderSettingsFilteringSceneIndex::GetFallbackPrimPath();
#else
    const SdfPath& renderScope = SdfPath(_legacyTokens->renderScope);
    const SdfPath& fallbackPrimPath = SdfPath(_legacyTokens->fallbackPath);
#endif

    for (const SdfPath& path : HdSceneIndexPrimView(si, renderScope)) {
        if (path != fallbackPrimPath &&
            si->GetPrim(path).primType == HdPrimTypeTokens->renderSettings) {
            return true;
        }
    }
    return false;
}

/// Resolves the shutter interval from the render product and camera.
///
/// @param product The render product.
/// @param camera The camera.
/// @return The shutter interval.
GfVec2f
_ResolveShutterInterval(
    HdRenderSettings::RenderProduct const& product,
    const HdCamera* camera)
{
    if (product.disableMotionBlur) {
        return GfVec2f(0.0f);
    }

    // Default 180-degree shutter
    GfVec2f shutter(0.0f, 0.5f);
    
    if (camera) {
        shutter[0] = camera->GetShutterOpen();
        shutter[1] = camera->GetShutterClose();
    }

    return shutter;
}

/// Sets Arnold options and executes rendering.
///
/// @param camera The camera to render from.
/// @param arnoldOptions The Arnold options.
/// @param shutter The shutter interval.
/// @param interactive Whether this is an interactive render.
/// @param param The Arnold render param.
/// @return True if rendering succeeded.
bool
_SetOptionsAndRender(
    const HdCamera* camera,
    VtDictionary const& arnoldOptions,
    GfVec2f const& shutter,
    bool interactive,
    HdArnoldRenderParam* param,
    HdArnoldRenderDelegate* renderDelegate)
{
    if (!camera) {
        TF_CODING_ERROR("Invalid camera provided for rendering.\n");
        return false;
    }

    // Apply Arnold options
    AtNode* options = AiUniverseGetOptions(renderDelegate->GetUniverse());

    // Set Arnold options from the render settings
    for (auto const& pair : arnoldOptions) {
        const std::string& name = pair.first;
        const VtValue& value = pair.second;

        // Convert VtValue to Arnold parameter
        const AtParamEntry* paramEntry = AiNodeEntryLookUpParameter(
            AiNodeGetNodeEntry(options), AtString(name.c_str()));
        
        if (!paramEntry) {
            TF_WARN("Unknown Arnold option: %s", name.c_str());
            continue;
        }

        HdArnoldSetParameter(options, paramEntry, value, renderDelegate);
    }

    // Set shutter interval
    AiNodeSetFlt(options, AtString("shutter_start"), shutter[0]);
    AiNodeSetFlt(options, AtString("shutter_end"), shutter[1]);

    // Rendering is handled by the render param through AiRenderBegin/AiRenderEnd
    // For batch rendering, we would call param->UpdateRender() which internally
    // manages the Arnold render session.
    
    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
        "Arnold options configured for rendering (interactive=%d)\n", interactive);

    // In the UpdateAndRender context, the actual render execution would be
    // managed externally by the render pass. Here we just configure the options.
    // The render will be executed by subsequent calls to param->UpdateRender().

    return true;
}

} // end anonymous namespace

// ----------------------------------------------------------------------------

HdArnoldRenderSettings::HdArnoldRenderSettings(SdfPath const& id)
    : HdRenderSettings(id)
{
}

HdArnoldRenderSettings::~HdArnoldRenderSettings() = default;

bool
HdArnoldRenderSettings::DriveRenderPass(
    bool interactive,
    bool renderPassHasAovBindings) const
{
    // Scenarios where we use the render settings prim to drive render pass:
    // 1. In batch rendering (e.g., usdrecord) when explicitly enabled via
    //    HDARNOLD_RENDER_SETTINGS_DRIVE_RENDER_PASS environment variable.
    // 2. When the render task does not have AOV bindings.
    //
    // Interactive viewport rendering currently relies on AOV bindings from
    // the task and is not yet supported via render settings prim.

    static const bool driveRenderPassWithAovBindings =
        TfGetEnvSetting(HDARNOLD_RENDER_SETTINGS_DRIVE_RENDER_PASS);

    const bool result =
        IsValid() &&
        (driveRenderPassWithAovBindings || !renderPassHasAovBindings) &&
        !interactive;

    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
        "DriveRenderPass = %d\n"
        " - HDARNOLD_RENDER_SETTINGS_DRIVE_RENDER_PASS = %d\n"
        " - valid = %d\n"
        " - interactive = %d\n"
        " - renderPassHasAovBindings = %d\n",
        result, driveRenderPassWithAovBindings, IsValid(), 
        interactive, renderPassHasAovBindings);

    return result;
}

bool
HdArnoldRenderSettings::UpdateAndRender(
    const HdRenderIndex* renderIndex,
    bool interactive,
    HdArnoldRenderParam* param)
{
    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
        "UpdateAndRender called for render settings prim %s\n",
        GetId().GetText());

    if (!IsValid()) {
        TF_CODING_ERROR(
            "Render settings prim %s does not have valid render products.\n",
            GetId().GetText());
        return false;
    }

    if (interactive) {
        TF_CODING_ERROR(
            "Support for driving interactive renders using a render settings "
            "prim is not yet available.\n");
        return false;
    }

    bool success = true;

    // Get the render delegate
    HdArnoldRenderDelegate* renderDelegate = 
        static_cast<HdArnoldRenderDelegate*>(renderIndex->GetRenderDelegate());

    const size_t numProducts = GetRenderProducts().size();

    // Process each render product
    for (size_t prodIdx = 0; prodIdx < numProducts; prodIdx++) {
        auto const& product = GetRenderProducts().at(prodIdx);

        if (product.renderVars.empty()) {
            TF_WARN("Skipping empty render product %s\n",
                    product.name.GetText());
            continue;
        }

        TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
            "Processing render product %s\n", product.name.GetText());

        // Get the camera
        const HdCamera* camera = nullptr;
        if (!product.cameraPath.IsEmpty()) {
            camera = dynamic_cast<const HdCamera*>(
                renderIndex->GetSprim(HdPrimTypeTokens->camera, 
                                     product.cameraPath));
        }

        if (!camera) {
            TF_WARN("Invalid camera path for render product %s: %s\n",
                    product.name.GetText(), 
                    product.cameraPath.GetText());
            continue;
        }

        // Resolve shutter interval
        const GfVec2f shutter = _ResolveShutterInterval(product, camera);

        // Configure outputs based on render vars
        _ProcessRenderProducts(param);

        // Set options and render
        const bool result = _SetOptionsAndRender(
            camera,
            _arnoldOptions,
            shutter,
            interactive,
            param,
            renderDelegate);

        if (TfDebug::IsEnabled(HDARNOLD_RENDER_SETTINGS)) {
            if (result) {
                TfDebug::Helper().Msg(
                    "Rendered product %s successfully\n", 
                    product.name.GetText());
            } else {
                TfDebug::Helper().Msg(
                    "Failed to render product %s\n", 
                    product.name.GetText());
            }
        }

        success = success && result;
    }

    return success;
}

void
HdArnoldRenderSettings::Finalize(HdRenderParam* renderParam)
{
    // HdArnoldRenderParam* param = 
    //     static_cast<HdArnoldRenderParam*>(renderParam);

    // Clean up any resources associated with this render settings prim
    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
        "Finalizing render settings prim %s\n", GetId().GetText());

    // TODO: Clean up Arnold-specific resources if needed
}

void
HdArnoldRenderSettings::_Sync(
    HdSceneDelegate* sceneDelegate,
    HdRenderParam* renderParam,
    const HdDirtyBits* dirtyBits)
{
    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
        "Syncing render settings prim %s (dirty bits = %x)\n",
        GetId().GetText(), *dirtyBits);

    HdArnoldRenderParam* param = 
        static_cast<HdArnoldRenderParam*>(renderParam);

    const VtDictionary& namespacedSettings = GetNamespacedSettings();

    HdSceneIndexBaseRefPtr terminalSi =
        sceneDelegate->GetRenderIndex().GetTerminalSceneIndex();

    // Only process if we have a non-fallback render settings prim
    if (!_HasNonFallbackRenderSettingsPrim(terminalSi)) {
        TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
            "No non-fallback render settings prim found, skipping sync\n");
        return;
    }

    // Generate Arnold options from the namespaced settings
    _arnoldOptions = _GenerateArnoldOptions(namespacedSettings);

    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
        "Generated %zu Arnold options from render settings\n",
        _arnoldOptions.size());

    // Process render terminals (integrators, imagers, etc.)
    _ProcessRenderTerminals(sceneDelegate, param);
}

#if PXR_VERSION <= 2308
bool
HdArnoldRenderSettings::IsValid() const
{
    // A render settings prim is valid if it has at least one render product
    // with at least one render var
    const RenderProducts& products = GetRenderProducts();
    
    for (const auto& product : products) {
        if (!product.renderVars.empty()) {
            return true;
        }
    }
    
    return false;
}
#endif

void
HdArnoldRenderSettings::_ProcessRenderTerminals(
    HdSceneDelegate* sceneDelegate,
    HdArnoldRenderParam* param)
{
    // Process render terminal connections such as integrators and imagers
    // These are typically connected as relationships on the render settings prim

    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
        "Processing render terminals for %s\n", GetId().GetText());

    // TODO: Implement processing of arnold:integrator connection
    // TODO: Implement processing of arnold:imagers connection
}

void
HdArnoldRenderSettings::_ProcessRenderProducts(HdArnoldRenderParam* param)
{
    // Process render products and configure Arnold outputs (drivers)
    
    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
        "Processing render products for %s\n", GetId().GetText());

    const RenderProducts& products = GetRenderProducts();

    for (const auto& product : products) {
        TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg(
            "  Product: %s (%zu render vars)\n",
            product.name.GetText(),
            product.renderVars.size());

        // TODO: Configure Arnold outputs/drivers based on render vars
        // TODO: Set resolution from product.resolution
        // TODO: Set pixel aspect ratio from product.pixelAspectRatio
        // TODO: Set data window from product.dataWindowNDC
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_VERSION >= 2308
