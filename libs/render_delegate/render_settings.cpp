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
#ifdef ENABLE_HYDRA2_RENDERSETTINGS

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

#include <pxr/usd/usdRender/tokens.h>
#include <pxr/usdImaging/usdImaging/renderSettingsAdapter.h>
#include <pxr/usdImaging/usdImaging/usdRenderSettingsSchema.h>

#include <iostream>
#include <string>
#include "../common/rendersettings_utils.h"
#include "camera.h"

#include "render_delegate.h"
#include "render_param.h"
#include "utils.h"

PXR_NAMESPACE_OPEN_SCOPE

// Debug codes for render settings
TF_DEBUG_CODES(HDARNOLD_RENDER_SETTINGS);

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(HDARNOLD_RENDER_SETTINGS, "Debug logging for Arnold render settings prim.");
}

// Environment variable to control whether render settings prim drives the
// render pass. This allows comparison between legacy AOV bindings and
// render settings prim-driven rendering.
TF_DEFINE_ENV_SETTING(
    HDARNOLD_RENDER_SETTINGS_DRIVE_RENDER_PASS, false,
    "Drive the render pass using the first RenderProduct on the render "
    "settings prim when the render pass has AOV bindings.");

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
     // Data types
    (color3f));

#if PXR_VERSION <= 2308
TF_DEFINE_PRIVATE_TOKENS(
    _legacyTokens,
    ((fallbackPath,
      "/Render/__HdsiRenderSettingsFilteringSceneIndex__FallbackSettings"))((renderScope, "/Render"))(shutterInterval));
#endif

namespace {

/// Translates a settings property name to an Arnold option name.
///
/// Strips the "arnold:" prefix from property names.
/// Example: "arnold:AA_samples" -> "AA_samples"
///
/// @param propertyName The property name from the render settings.
/// @return The Arnold option name.
std::string _GetArnoldOptionName(std::string const& propertyName)
{
    if (TfStringStartsWith(propertyName, "arnold:global:")) {
        return propertyName.substr(14); // strlen("arnold:global:")
    }

    // Strip "arnold:" prefix if present
    if (TfStringStartsWith(propertyName, "arnold:")) {
        return propertyName.substr(7); // strlen("arnold:")
    }

    TF_WARN("Could not translate settings property %s to Arnold option name.", propertyName.c_str());
    return "";
}

/// Generates a dictionary of Arnold options from render settings.
///
/// @param settings The render settings dictionary.
/// @return A dictionary of Arnold options.
VtDictionary _GenerateArnoldOptions(VtDictionary const& settings)
{
    VtDictionary options;
    for (auto const& pair : settings) {
        const std::string& name = pair.first;
        const TfToken tokenName(name);

        const std::string arnoldName = _GetArnoldOptionName(name);
        if (!arnoldName.empty()) {
            options[arnoldName] = pair.second;
        }
    }

    return options;
}

} // end anonymous namespace

// ----------------------------------------------------------------------------

HdArnoldRenderSettings::HdArnoldRenderSettings(SdfPath const& id) : HdRenderSettings(id) {}

HdArnoldRenderSettings::~HdArnoldRenderSettings()
{ 
    // We might want to reset the camera on the render delegate
    // renderDelegate->SetHydraRenderSettingsPath(SdfPath()); 
};

void HdArnoldRenderSettings::Finalize(HdRenderParam* renderParam)
{
    HdArnoldRenderParam* param = static_cast<HdArnoldRenderParam*>(renderParam);

    // Clean up any resources associated with this render settings prim
    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg("Finalizing render settings prim %s\n", GetId().GetText());

    // If this was the active render settings prim, clear it
    if (param->GetHydraRenderSettingsPrimPath() == GetId()) {
        param->SetHydraRenderSettingsPrimPath(SdfPath());
    }
}

void HdArnoldRenderSettings::_UpdateArnoldOptions(HdSceneDelegate* sceneDelegate)
{
    // Generate Arnold options from the namespaced settings
    const VtDictionary& namespacedSettings = GetNamespacedSettings();
    const VtDictionary arnoldOptions = _GenerateArnoldOptions(namespacedSettings);

    if (arnoldOptions.empty()) {
        return;
    }

    HdArnoldRenderDelegate* renderDelegate =
        static_cast<HdArnoldRenderDelegate*>(sceneDelegate->GetRenderIndex().GetRenderDelegate());
    AtNode* options = renderDelegate->GetOptions();

    // Set Arnold options from the render settings
    for (auto const& pair : arnoldOptions) {
        const std::string& name = pair.first;
        const VtValue& value = pair.second;

        // Convert VtValue to Arnold parameter
        const AtParamEntry* paramEntry =
            AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(options), AtString(name.c_str()));

        if (!paramEntry) {
            TF_WARN("Unknown Arnold option: %s", name.c_str());
            continue;
        }
        // NOTE: the handling of the atmosphere, background, shader_override, aov_shaders and operator are all managed
        // in the HdArnoldSetParameter. The connections are resolved later on using an alias system
        // Except when atmosphere and background are connected to "sub outputs" outputs:environment outputs:background. Still to fix
        HdArnoldSetParameter(options, paramEntry, value, renderDelegate);
    }

    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg("Set %zu Arnold options from render settings\n", arnoldOptions.size());
}

void HdArnoldRenderSettings::_UpdateShutterInterval(HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param)
{
    if (GetShutterInterval().IsHolding<GfVec2d>()) {
        // Set the shutter interval on the render delegate
        const GfVec2d shutterInterval = GetShutterInterval().UncheckedGet<GfVec2d>();
        // The shutter is stored in
        //  * ArnoldRenderParams
        //  * ArnoldHydraReader when used with (kick)
        //  * Arnold universe
        //  * this render settings
        _hydraCameraShutter = GfVec2f(shutterInterval[0], shutterInterval[1]);
    
        // First update the render params with the new shutter interval
        param->UpdateShutter(_hydraCameraShutter);

        // The next call to _Execute will replace param->_shutter with the value of the universe
        // We also update Arnold directly
        HdArnoldRenderDelegate* renderDelegate =
            static_cast<HdArnoldRenderDelegate*>(sceneDelegate->GetRenderIndex().GetRenderDelegate());

        if (renderDelegate) {
            AtNode* options = renderDelegate->GetOptions();
            if (options) {
                AiNodeSetFlt(options, AtString("shutter_start"), static_cast<float>(shutterInterval[0]));
                AiNodeSetFlt(options, AtString("shutter_end"), static_cast<float>(shutterInterval[1]));

                TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                    .Msg("Set shutter interval to [%f, %f]\n", shutterInterval[0], shutterInterval[1]);
            }
        }
    }
}

void HdArnoldRenderSettings::_UpdateRenderingColorSpace(HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param)
{
#if PXR_VERSION >= 2211
    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg("Updating rendering color space for %s\n", GetId().GetText());

    HdArnoldRenderDelegate* renderDelegate =
        static_cast<HdArnoldRenderDelegate*>(sceneDelegate->GetRenderIndex().GetRenderDelegate());

    if (!renderDelegate) {
        return;
    }

    AtNode* options = renderDelegate->GetOptions();
    if (!options) {
        return;
    }

    // Get the render settings prim from the terminal scene index
    HdSceneIndexBaseRefPtr terminalSi = sceneDelegate->GetRenderIndex().GetTerminalSceneIndex();
    if (!terminalSi) {
        return;
    }

    HdSceneIndexPrim prim = terminalSi->GetPrim(GetId());
    if (!prim.dataSource) {
        return;
    }

    // Get USD rendering color space from the data source
    UsdImagingUsdRenderSettingsSchema usdRss = UsdImagingUsdRenderSettingsSchema::GetFromParent(prim.dataSource);

    // Setup color manager - check for OCIO environment variable first
    AtNode* colorManager = nullptr;
    const char* ocio_path = std::getenv("OCIO");
    if (ocio_path) {
        colorManager = renderDelegate->CreateArnoldNode(AtString("color_manager_ocio"), AtString("color_manager_ocio"));
        if (colorManager) {
            AiNodeSetStr(colorManager, str::config, AtString(ocio_path));
        }
    }

    // If no OCIO environment variable, use the default color manager
    if (colorManager == nullptr) {
        colorManager = AiNodeLookUpByName(AiNodeGetUniverse(options), str::ai_default_color_manager_ocio);
    }

    if (colorManager) {
        // Set the color manager node in the options
        AiNodeSetPtr(options, str::color_manager, colorManager);

        // Set rendering color space from USD if available
        HdTokenDataSourceHandle renderingColorSpaceHandle = usdRss.GetRenderingColorSpace();
        if (renderingColorSpaceHandle) {
            TfToken renderingColorSpace = renderingColorSpaceHandle->GetTypedValue(0.0f);
            if (!renderingColorSpace.IsEmpty()) {
                AiNodeSetStr(colorManager, str::color_space_linear, AtString(renderingColorSpace.GetText()));

                TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                    .Msg("Set rendering color space to: %s\n", renderingColorSpace.GetText());
            }
        }

        // Set color manager parameters from arnold:color_manager prefixed settings
        const VtDictionary& namespacedSettings = GetNamespacedSettings();
        const std::string colorManagerPrefix = "arnold:color_manager:";
        
        for (const auto& pair : namespacedSettings) {
            const std::string& settingName = pair.first;
            
            // Only process arnold:color_manager: prefixed settings
            if (!TfStringStartsWith(settingName, colorManagerPrefix)) {
                continue;
            }
            
            // Extract parameter name by stripping the prefix
            std::string paramName = settingName.substr(colorManagerPrefix.size());
            
            // Look up the parameter in the color manager's node entry
            const AtParamEntry* paramEntry =
                AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(colorManager), AtString(paramName.c_str()));
            
            if (!paramEntry) {
                TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                    .Msg("Unknown color manager parameter: %s\n", paramName.c_str());
                continue;
            }
            
            // Set the parameter value using HdArnoldSetParameter
            HdArnoldSetParameter(colorManager, paramEntry, pair.second, renderDelegate);
            
            TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                .Msg("Set color manager parameter %s on %s\n", paramName.c_str(), AiNodeGetName(colorManager));
        }

        TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg("Updated color manager: %s\n", AiNodeGetName(colorManager));
    }
#endif
}

void HdArnoldRenderSettings::_ReadUsdRenderSettings(HdSceneDelegate* sceneDelegate)
{
    HdSceneIndexBaseRefPtr terminalSi = sceneDelegate->GetRenderIndex().GetTerminalSceneIndex();
    if (terminalSi) {
        HdSceneIndexPrim prim = terminalSi->GetPrim(GetId());
        if (prim) {
            HdArnoldRenderDelegate* renderDelegate =
                static_cast<HdArnoldRenderDelegate*>(sceneDelegate->GetRenderIndex().GetRenderDelegate());
            UsdImagingUsdRenderSettingsSchema usdRss =
                UsdImagingUsdRenderSettingsSchema::GetFromParent(prim.dataSource);
            AtNode* options = renderDelegate->GetOptions();
            if (!options) return;
            HdFloatDataSourceHandle parHandle = usdRss.GetPixelAspectRatio();
            if (parHandle) {
                AiNodeSetFlt(options, str::pixel_aspect_ratio, parHandle->GetTypedValue(0));
            }

            HdVec2iDataSourceHandle resHandle = usdRss.GetResolution();
            if (resHandle) {
                GfVec2i res = resHandle->GetTypedValue(0);
                AiNodeSetInt(options, str::xres, res[0]);
                AiNodeSetInt(options, str::yres, res[1]);
            }

            HdVec4fDataSourceHandle dwndcHandle = usdRss.GetDataWindowNDC();
            if (dwndcHandle) {
                GfVec2i resolution;
                resolution[0] = AiNodeGetInt(options, str::xres);
                resolution[1] = AiNodeGetInt(options, str::yres);
                SetRegion(options, dwndcHandle->GetTypedValue(0), resolution);
            }

            // NOTE: Unfortunatelly we don't have access to instantaneousShutter which is deprecated but is
            // used in some of the tests. We use GetDisableMotionBlur which is the replacement for instantaneousShutter
            HdBoolDataSourceHandle mbHandle = usdRss.GetDisableMotionBlur();
            if (mbHandle) {
                AiNodeSetBool(options, str::ignore_motion_blur, mbHandle->GetTypedValue(0));
            }
            // TODO we might want to reset

            HdPathDataSourceHandle camHandle = usdRss.GetCamera();
            if (camHandle) {
                _hydraCameraPath = SdfPath(camHandle->GetTypedValue(0));
                const AtParamEntry* paramEntry = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(options), str::camera);
                HdArnoldSetParameter(options, paramEntry, VtValue(_hydraCameraPath.GetString()), renderDelegate);
            } else {
                // Should we reset the camera ? In batch mode it's not necessary but we might want to do it in interactive mode
                _hydraCameraPath = SdfPath();
            }
        }
    }
}

void HdArnoldRenderSettings::_Sync(
    HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, const HdDirtyBits* dirtyBits)
{
    if (std::getenv("USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX") == nullptr)
        return;

    TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
        .Msg("Syncing render settings prim %s (dirty bits = %x)\n", GetId().GetText(), *dirtyBits);

    HdSceneIndexBaseRefPtr terminalSi = sceneDelegate->GetRenderIndex().GetTerminalSceneIndex();

    // Test if this render setting is the one chosen
    SdfPath renderSettingPrimPath;

    const bool hasActiveRsp = HdUtils::HasActiveRenderSettingsPrim(terminalSi, &renderSettingPrimPath);
    if (!hasActiveRsp || renderSettingPrimPath != GetId()) {
        // TODO Set dirty bits clean and exit
        // TODO we should also check we are in a procedural children,
        // in that case we don't want to use those render settings
        return;
    }
    HdArnoldRenderDelegate* renderDelegate =
        static_cast<HdArnoldRenderDelegate*>(sceneDelegate->GetRenderIndex().GetRenderDelegate());
    if (renderDelegate->GetProceduralParent())
        return;

    // We register this render setting as the one to use for the render.
    HdArnoldRenderParam* param = static_cast<HdArnoldRenderParam*>(renderParam);
    param->SetHydraRenderSettingsPrimPath(GetId());

    // TODO when do we need to read them ? just only once ?
    // What happens when the resolution is changed in the render settings ?
    _ReadUsdRenderSettings(sceneDelegate);

    // TODO
    // DirtyActive
    // DirtyIncludedPurposes
    // DirtyMaterialBindingPurposes
    // DirtyFrameNumber

    if (*dirtyBits & HdRenderSettings::DirtyNamespacedSettings) {
        // Generate and apply Arnold options from the render settings
        _UpdateArnoldOptions(sceneDelegate);
    }

#if PXR_VERSION >= 2311
    if (*dirtyBits & HdRenderSettings::DirtyShutterInterval || *dirtyBits & HdRenderSettings::DirtyNamespacedSettings) {
        _UpdateShutterInterval(sceneDelegate, param);
    }
#endif

    if (*dirtyBits & DirtyRenderProducts) {
        // TODO implement _UpdateRenderProduct
        _UpdateRenderProducts(sceneDelegate, param);
    }

    if (*dirtyBits & DirtyRenderingColorSpace) {
        _UpdateRenderingColorSpace(sceneDelegate, param);
    }

}

#if PXR_VERSION <= 2308
bool HdArnoldRenderSettings::IsValid() const
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

void HdArnoldRenderSettings::_UpdateRenderProducts(HdSceneDelegate* sceneDelegate, HdArnoldRenderParam* param)
{
    // TODO : the filter is not mapped correctly
    TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg("Updating render products for %s\n", GetId().GetText());

    const RenderProducts& products = GetRenderProducts();
    if (products.empty()) {
        return;
    }

    HdArnoldRenderDelegate* renderDelegate =
        static_cast<HdArnoldRenderDelegate*>(sceneDelegate->GetRenderIndex().GetRenderDelegate());
    AtNode* options = renderDelegate->GetOptions();

    std::vector<std::string> outputs;
    std::vector<std::string> lpes;
    std::vector<AtNode*> aovShaders;
    std::set<AtNode*> beautyDrivers;

    // Process each render product
    for (const auto& product : products) {
        if (product.renderVars.empty()) {
            TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg("Empty render product %s\n", product.name.GetText());
        }

        // Check if this product has a specific resolution set
        // If so, use it instead of the global render settings resolution
        // Note that this sets the last product resolution found.
        if (product.resolution[0] > 0 && product.resolution[1] > 0) {
            AiNodeSetInt(options, str::xres, product.resolution[0]);
            AiNodeSetInt(options, str::yres, product.resolution[1]);
            TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                .Msg("Using product resolution: %dx%d\n", product.resolution[0], product.resolution[1]);
        }

        // Create driver node
        std::string driverType = "driver_exr"; // Default driver type
        std::string driverName = product.productPath.GetString();
        std::string filename = product.name.GetString(); // == productName

        // Check for extension to determine driver type
        std::string extension = TfGetExtension(filename);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (extension == "tif") {
            driverType = "driver_tiff";
        } else if (extension == "jpg" || extension == "jpeg") {
            driverType = "driver_jpeg";
        } else if (extension == "png") {
            driverType = "driver_png";
        } else if (filename.size() && extension.empty()) {
            filename += ".exr";
        }
        if (product.type == TfToken("deep")) {
            driverType = "driver_deepexr";
        }

        // Find arnold:driver override 
        const VtDictionary& productSettings = product.namespacedSettings;
        auto driverTypeIt = productSettings.find("arnold:driver");
        if (driverTypeIt != productSettings.end()) {
            const VtValue& driverTypeValue = driverTypeIt->second;
            if (driverTypeValue.IsHolding<std::string>()) {
                driverType = driverTypeValue.UncheckedGet<std::string>();
            } else if (driverTypeValue.IsHolding<TfToken>()) {
                driverType = driverTypeValue.UncheckedGet<TfToken>().GetString();
            }
        }

        AtNode* driver = renderDelegate->CreateArnoldNode(AtString(driverType.c_str()), AtString(driverName.c_str()));

        if (!driver) {
            TF_WARN("Failed to create driver for render product %s\n", driverName.c_str());
            continue;
        }

        AiNodeSetStr(driver, str::filename, AtString(filename.c_str()));
        const char* driverNodeName = AiNodeGetName(driver);

        // Set driver parameters from product's arnold-namespaced settings
        const std::string driverPrefix = "arnold:" + driverType + ":";
        
        for (const auto& pair : productSettings) {
            const std::string& settingName = pair.first;

            // Only process arnold-prefixed settings
            if (!TfStringStartsWith(settingName, "arnold:")) {
                continue;
            }

            // Extract parameter name by stripping "arnold:" or "arnold:driverType:" prefix
            std::string paramName;
            if (TfStringStartsWith(settingName, driverPrefix)) {
                paramName = settingName.substr(driverPrefix.size());
            } else if (TfStringStartsWith(settingName, "arnold:")) {
                paramName = settingName.substr(7); // strlen("arnold:")
            } else {
                continue;
            }

            if (paramName == "driver")
                continue;
            // Look up the parameter in the driver's node entry
            const AtParamEntry* paramEntry =
                AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(driver), AtString(paramName.c_str()));

            if (!paramEntry) {
                TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                    .Msg("Unknown driver parameter: %s for driver %s\n", paramName.c_str(), driverNodeName);
                continue;
            }

            // Set the parameter value using HdArnoldSetParameter
            HdArnoldSetParameter(driver, paramEntry, pair.second, renderDelegate);

            TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                .Msg("Set driver parameter %s on %s\n", paramName.c_str(), driverNodeName);
        }
        
        // If the driver was renamed using arnold:name, we want to use its new name
        driverNodeName = AiNodeGetName(driver);
        
        // Set imager on the driver if arnold:global:imager is specified in the render settings
        const VtDictionary& namespacedSettings = GetNamespacedSettings();
        auto imagerIt = namespacedSettings.find("arnold:global:imager");
        if (imagerIt != namespacedSettings.end()) {
            const VtValue& imagerValue = imagerIt->second;
            const AtParamEntry* paramEntry =
                AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(driver), str::input);
            
            if (paramEntry) {
                HdArnoldSetParameter(driver, paramEntry, imagerValue, renderDelegate);
                
                TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                    .Msg("Set imager on driver %s\n", driverNodeName);
            }
        }
        // TODO handle resolution / pixelAspectRatio / apertureSize / dataWindowNDC 
        // Track AOV names to detect duplicates
        std::unordered_set<std::string> aovNames;
        std::unordered_set<std::string> duplicatedAovs;
        std::vector<std::string> layerNames;
        std::vector<std::string> aovNamesList;
        size_t prevOutputsCount = outputs.size();
        bool useLayerName = false;
        std::vector<bool> isHalfList;
        bool isDriverExr = AiNodeIs(driver, str::driver_exr);

        // Process render vars for this product
        for (const auto& renderVar : product.renderVars) {
            // Create filter (default to box_filter)
            std::string varName = renderVar.varPath.GetString();
            std::string filterName = varName + "/filter";
            std::string filterType = "box_filter";

            // Check if arnold:filter is specified in the render var settings
            const VtDictionary& renderVarSettings = renderVar.namespacedSettings;
            auto filterIt = renderVarSettings.find("arnold:filter");
            if (filterIt != renderVarSettings.end()) {
                const VtValue& filterValue = filterIt->second;
                if (filterValue.IsHolding<std::string>()) {
                    filterType = filterValue.UncheckedGet<std::string>();
                } else if (filterValue.IsHolding<TfToken>()) {
                    filterType = filterValue.UncheckedGet<TfToken>().GetString();
                }
            }

            AtNode* filter = AiNodeLookUpByName(AiNodeGetUniverse(options), AtString(filterName.c_str()));
            if (!filter) {
                filter = renderDelegate->CreateArnoldNode(AtString(filterType.c_str()), AtString(filterName.c_str()));
            }

            if (!filter) {
                TF_WARN("Failed to create filter for render var %s\n", varName.c_str());
                continue;
            }

            const char* filterNodeName = AiNodeGetName(filter);

            // Set filter parameters from render var's arnold-namespaced settings
            for (const auto& pair : renderVarSettings) {
                const std::string& settingName = pair.first;

                // Only process arnold-prefixed settings
                if (!TfStringStartsWith(settingName, "arnold:")) {
                    continue;
                }

                // Skip arnold:filter since it's used to determine filter type, not as a parameter
                if (settingName == "arnold:filter") {
                    continue;
                }

                // Extract parameter name by stripping "arnold:" or "arnold:filterType:" prefix
                std::string paramName;
                const std::string filterPrefix = "arnold:" + filterType + ":";
                if (TfStringStartsWith(settingName, filterPrefix)) {
                    paramName = settingName.substr(filterPrefix.size());
                } else if (TfStringStartsWith(settingName, "arnold:globals:")) {
                    paramName = settingName.substr(15); // strlen("arnold:globals:")
                } else if (TfStringStartsWith(settingName, "arnold:")) {
                    paramName = settingName.substr(7); // strlen("arnold:")
                } else {
                    continue;
                }
                // Look up the parameter in the filter's node entry
                const AtParamEntry* paramEntry =
                    AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(filter), AtString(paramName.c_str()));

                if (!paramEntry) {
                    TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                        .Msg("Unknown filter parameter: %s for filter %s\n", paramName.c_str(), filterNodeName);
                    continue;
                }

                // Set the parameter value using HdArnoldSetParameter
                HdArnoldSetParameter(filter, paramEntry, pair.second, renderDelegate);

                TF_DEBUG(HDARNOLD_RENDER_SETTINGS)
                    .Msg("Set filter parameter %s on %s\n", paramName.c_str(), filterNodeName);
            }
            // The filter might have been renamed.
            filterNodeName = AiNodeGetName(filter);
            // Get data type
            TfToken dataType = renderVar.dataType;
            if (dataType.IsEmpty()) {
                dataType = _tokens->color3f; // default
            }

            // Override with the driver:parameters:aov:format
            auto aovDriverFormatIt = renderVarSettings.find("driver:parameters:aov:format");
            if (aovDriverFormatIt != renderVarSettings.end()) {
                const VtValue& aovDriverFormatValue = aovDriverFormatIt->second;
                if (aovDriverFormatValue.IsHolding<TfToken>()) {
                    dataType = aovDriverFormatValue.UncheckedGet<TfToken>();
                } else if (aovDriverFormatValue.IsHolding<std::string>()) {
                    dataType = TfToken(aovDriverFormatValue.UncheckedGet<std::string>());
                }
            }

            // If the attribute arnold:format is present, it overrides the dataType attr
            // (this is needed for cryptomatte in Hydra #1164)
            auto arnoldFormatIt = renderVarSettings.find("arnold:format");
            if (arnoldFormatIt != renderVarSettings.end()) {
                const VtValue& arnoldFormatValue = arnoldFormatIt->second;
                if (arnoldFormatValue.IsHolding<TfToken>()) {
                    dataType = arnoldFormatValue.UncheckedGet<TfToken>();
                } else if (arnoldFormatValue.IsHolding<std::string>()) {
                    dataType = TfToken(arnoldFormatValue.UncheckedGet<std::string>());
                }
            }

            const ArnoldAOVTypes arnoldTypes = GetArnoldTypesFromFormatToken(dataType);

            // Get source name and type
            std::string sourceName = renderVar.sourceName;
            if (sourceName.empty() || sourceName == "color") {
                sourceName = "RGBA";
            }

            TfToken sourceType = renderVar.sourceType;
            std::string aovName = sourceName;
            std::string layerName = renderVar.varPath.GetName();
            bool hasLayerName = false;

            // Read the parameter "driver:parameters:aov:name" that will be needed if we have merged exrs (see #816)
            auto aovNameIt = renderVarSettings.find("driver:parameters:aov:name");
            if (aovNameIt != renderVarSettings.end()) {
                const VtValue& aovNameValue = aovNameIt->second;
                if (aovNameValue.IsHolding<std::string>()) {
                    std::string aovNameValueStr = aovNameValue.UncheckedGet<std::string>();
                    if (!aovNameValueStr.empty()) {
                        layerName = aovNameValueStr;
                        hasLayerName = true;
                    }
                } else if (aovNameValue.IsHolding<TfToken>()) {
                    std::string aovNameValueStr = aovNameValue.UncheckedGet<TfToken>().GetString();
                    if (!aovNameValueStr.empty()) {
                        layerName = aovNameValueStr;
                        hasLayerName = true;
                    }
                }
            }

            // Optional per-AOV camera
            // Initialize with product.cameraPath if available
            std::string cameraName;
            if (!product.cameraPath.IsEmpty()) {
                cameraName = product.cameraPath.GetString();
            }
            
            // Override with arnold:camera if specified in render var settings
            auto cameraIt = renderVarSettings.find("arnold:camera");
            if (cameraIt != renderVarSettings.end()) {
                const VtValue& cameraValue = cameraIt->second;
                if (cameraValue.IsHolding<std::string>()) {
                    cameraName = cameraValue.UncheckedGet<std::string>();
                } else if (cameraValue.IsHolding<TfToken>()) {
                    cameraName = cameraValue.UncheckedGet<TfToken>().GetString();
                }
            }

            // Handle different source types
            if (sourceType == UsdRenderTokens->lpe) {
                // Light Path Expression
                aovName = layerName;
                lpes.push_back(aovName + " " + sourceName);
            } else if (sourceType == UsdRenderTokens->primvar) {
                // Primvar AOV - requires aov_write and user_data shaders
                std::string aovShaderName = varName + "_shader";
                AtNode* aovShader =
                    renderDelegate->CreateArnoldNode(arnoldTypes.aovWrite, AtString(aovShaderName.c_str()));

                if (aovShader) {
                    AiNodeSetStr(aovShader, str::aov_name, AtString(aovName.c_str()));

                    std::string userDataName = varName + "_user_data";
                    AtNode* userData =
                        renderDelegate->CreateArnoldNode(arnoldTypes.userData, AtString(userDataName.c_str()));

                    if (userData) {
                        AiNodeLink(userData, "aov_input", aovShader);
                        AiNodeSetStr(userData, str::attribute, AtString(sourceName.c_str()));
                        aovShaders.push_back(aovShader);
                    }
                }
            }

            // Check for duplicates
            bool isDuplicatedAov = (hasLayerName && aovName != layerName);
            if (aovNames.find(sourceName) == aovNames.end()) {
                aovNames.insert(sourceName);
            } else {
                isDuplicatedAov = true;
            }

            if (isDuplicatedAov) {
                useLayerName = true;
                duplicatedAovs.insert(sourceName);
            }

            // Build output string
            std::string output;
            if (!cameraName.empty()) {
                output = cameraName + " ";
            }
            output += aovName + " " + arnoldTypes.outputString + " " + filterNodeName + " " + driverNodeName;

            // Track beauty drivers
            if (aovName == "RGBA") {
                beautyDrivers.insert(driver);
            }

            outputs.push_back(output);
            layerNames.push_back(layerName);
            aovNamesList.push_back(sourceName);
            isHalfList.push_back(isDriverExr ? arnoldTypes.isHalf : false);
        }

        // Add layer names for duplicated AOVs
        if (useLayerName) {
            for (size_t j = 0; j < layerNames.size(); ++j) {
                if (duplicatedAovs.find(aovNamesList[j]) != duplicatedAovs.end()) {
                    outputs[j + prevOutputsCount] += " " + layerNames[j];
                }
            }
        }

        // Set half precision for exr drivers
        if (!isHalfList.empty() && isDriverExr) {
            bool allHalf = true;
            for (size_t j = 0; j < isHalfList.size(); ++j) {
                if (isHalfList[j]) {
                    outputs[j + prevOutputsCount] += " HALF";
                } else {
                    allHalf = false;
                }
            }
            if (allHalf) {
                AiNodeSetBool(driver, AtString("half_precision"), true);
            }
        }
    }

    // Set outputs array on options
    if (!outputs.empty()) {
        AtArray* outputsArray = AiArrayAllocate(outputs.size(), 1, AI_TYPE_STRING);
        for (size_t i = 0; i < outputs.size(); ++i) {
            AiArraySetStr(outputsArray, i, AtString(outputs[i].c_str()));
        }
        AiNodeSetArray(options, str::outputs, outputsArray);

        TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg("Set %zu outputs on options\n", outputs.size());
    }

    // Set light path expressions
    if (!lpes.empty()) {
        AtArray* lpesArray = AiArrayAllocate(lpes.size(), 1, AI_TYPE_STRING);
        for (size_t i = 0; i < lpes.size(); ++i) {
            AiArraySetStr(lpesArray, i, AtString(lpes[i].c_str()));
        }
        AiNodeSetArray(options, str::light_path_expressions, lpesArray);

        TF_DEBUG(HDARNOLD_RENDER_SETTINGS).Msg("Set %zu light path expressions\n", lpes.size());
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_VERSION >= 2308
#endif // ENABLE_HYDRA2_RENDERSETTINGS
