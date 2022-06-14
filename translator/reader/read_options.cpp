// Copyright 2019 Autodesk, Inc.
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
#include "read_options.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/var.h>

#include <constant_strings.h>
#include <common_utils.h>

#include "registry.h"
#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((aovSettingFilter, "arnold:filter"))
    ((aovSettingWidth, "arnold:width"))
    ((aovSettingName,"driver:parameters:aov:name"))
    ((aovGlobalAtmosphere, "arnold:global:atmosphere"))
    ((aovGlobalBackground, "arnold:global:background"))
    ((aovGlobalAovs, "arnold:global:aov_shaders"))
    ((colorSpaceLinear, "arnold:global:color_space_linear"))
    ((colorSpaceNarrow, "arnold:global:color_space_narrow"))
    ((logFile, "arnold:global:log:file"))
    ((logVerbosity, "arnold:global:log:verbosity"))
    ((_float, "float"))
    ((_int, "int"))
    (ArnoldNodeGraph)
    (i8) (int8)
    (ui8) (uint8)
    (half) (float16)
    (float2) (float3) (float4)
    (half2) (half3) (half4)
    (color2f) (color3f) (color4f)
    (color2h) (color3h) (color4h)
    (color2u8) (color3u8) (color4u8)
    (color2i8) (color3i8) (color4i8)
    (int2) (int3) (int4)
    (uint2) (uint3) (uint4)
);


struct ArnoldAOVTypes {
    const char* outputString;
    const std::string aovWrite;
    const std::string userData;

    ArnoldAOVTypes(const char* _outputString, const char *_aovWrite, const char *_userData)
        : outputString(_outputString), aovWrite(_aovWrite), userData(_userData)
    {
    }
};

ArnoldAOVTypes _GetArnoldTypesFromTokenType(const TfToken& type)
{
    // We check for the most common cases first.
    if (type == _tokens->color3f) {
        return {"RGB", "aov_write_rgb", "user_data_rgb"};
    } else if (type == _tokens->color4f) {
        return {"RGBA", "aov_write_rgba", "user_data_rgba"};
    } else if (type == _tokens->float3) {
        return {"VECTOR", "aov_write_vector", "user_data_rgb"};
    } else if (type == _tokens->float2) {
        return {"VECTOR2", "aov_write_vector", "user_data_rgb"};
    } else if (type == _tokens->_float || type == _tokens->half || type == _tokens->float16) {
        return {"FLOAT", "aov_write_float", "user_data_float"};
    } else if (type == _tokens->_int || type == _tokens->i8 || type == _tokens->uint8) {
        return {"INT", "aov_write_int", "user_data_int"};
    } else if (
        type == _tokens->half2 || type == _tokens->color2f || type == _tokens->color2h || type == _tokens->color2u8 ||
        type == _tokens->color2i8 || type == _tokens->int2 || type == _tokens->uint2) {
        return {"VECTOR2", "aov_write_vector", "user_data_rgb"};
    } else if (type == _tokens->half3 || type == _tokens->int3 || type == _tokens->uint3) {
        return {"VECTOR", "aov_write_vector", "user_data_rgb"};
    } else if (
        type == _tokens->float4 || type == _tokens->half4 || type == _tokens->color4f || type == _tokens->color4h ||
        type == _tokens->color4u8 || type == _tokens->color4i8 || type == _tokens->int4 || type == _tokens->uint4) {
        return {"RGBA", "aov_write_rgba", "user_data_rgba"};
    } else {
        return {"RGB", "aov_write_rgb", "user_data_rgb"};
    }
}

// Read eventual connections to a ArnoldNodeGraph primitive, that acts as a passthrough
static inline void UsdArnoldNodeGraphConnection(AtNode *options, const UsdPrim &prim, const UsdAttribute &attr, 
                                            const std::string &attrName, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    VtValue value;
    if (attr && attr.Get(&value, time.frame)) {
        // RenderSettings have a string attribute, referencing a prim in the stage
        std::string valStr = VtValueGetString(value, &prim);
        if (!valStr.empty()) {
            SdfPath path(valStr);
            // We check if there is a primitive at the path of this string
            UsdPrim ngPrim = context.GetReader()->GetStage()->GetPrimAtPath(SdfPath(valStr));
            // We verify if the primitive is indeed a ArnoldNodeGraph
            if (ngPrim && ngPrim.GetTypeName() == _tokens->ArnoldNodeGraph) {
                // We can use a UsdShadeShader schema in order to read connections
                UsdShadeShader ngShader(ngPrim);
                // the output attribute must have the same name as the input one in the RenderSettings
                UsdShadeOutput outputAttr = ngShader.GetOutput(TfToken(attrName));
                if (outputAttr) {
                    SdfPathVector sourcePaths;
                    // Check which shader is connected to this output
                    if (outputAttr.HasConnectedSource() && outputAttr.GetRawConnectedSourcePaths(&sourcePaths) &&
                        !sourcePaths.empty()) {
                        SdfPath outPath(sourcePaths[0].GetPrimPath());
                        UsdPrim outPrim = context.GetReader()->GetStage()->GetPrimAtPath(outPath);
                        if (outPrim) {
                            context.AddConnection(options, attrName, outPath.GetText(), UsdArnoldReader::CONNECTION_PTR);
                        }
                    }
                }
            }
        }
    }
}

// Read eventual connections to a ArnoldNodeGraph primitive for the aov_shader shader array connections
static inline void UsdArnoldNodeGraphAovConnection(AtNode *options, const UsdPrim &prim, 
    const UsdAttribute &attr, const std::string &attrBase, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    VtValue value;
    if (attr && attr.Get(&value, time.frame)) {
        // RenderSettings have a string attribute, referencing a prim in the stage
        std::string valStr = VtValueGetString(value, &prim);
        if (!valStr.empty()) {
            SdfPath path(valStr);
            // We check if there is a primitive at the path of this string
            UsdPrim ngPrim = context.GetReader()->GetStage()->GetPrimAtPath(SdfPath(valStr));
            // We verify if the primitive is indeed a ArnoldNodeGraph
            if (ngPrim && ngPrim.GetTypeName() == _tokens->ArnoldNodeGraph) {
                AtArray* array = AiNodeGetArray(options, str::aov_shaders);
                unsigned numElements = AiArrayGetNumElements(array);
                // We can use a UsdShadeShader schema in order to read connections
                UsdShadeShader ngShader(ngPrim);
                for (unsigned i=1;; i++) {
                    // the output terminal name will be aov_shader{1,...,n} as a contiguous array
                    TfToken outputName(attrBase + std::to_string(i));
                    UsdShadeOutput outputAttr = ngShader.GetOutput(outputName);
                    if (!outputAttr)
                        break;
                    SdfPathVector sourcePaths;
                    // Check which shader is connected to this output
                    if (outputAttr.HasConnectedSource() && outputAttr.GetRawConnectedSourcePaths(&sourcePaths) &&
                        !sourcePaths.empty()) {
                        SdfPath outPath(sourcePaths[0].GetPrimPath());
                        UsdPrim outPrim = context.GetReader()->GetStage()->GetPrimAtPath(outPath);
                        if (outPrim) {
                            // we connect to aov_shaders{0,...,n-1} parameters i.e. 0 indexed, offset from any previous connections
                            unsigned index = numElements + i-1;
                            std::string outputElement = attrBase + "[" + std::to_string(index) + "]";
                            context.AddConnection(options, outputElement, outPath.GetText(),
                                                  UsdArnoldReader::CONNECTION_PTR);
                        }
                    }
                }
            }
        }
    }
}

void UsdArnoldReadRenderSettings::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    // No need to create any node in arnold, since the options node is automatically created
    AtNode *options = AiUniverseGetOptions(context.GetReader()->GetUniverse());
    const TimeSettings &time = context.GetTimeSettings();

    UsdRenderSettings renderSettings(prim);
    if (!renderSettings)
        return;

    VtValue pixelAspectRatioValue;
    if (renderSettings.GetPixelAspectRatioAttr().Get(&pixelAspectRatioValue, time.frame))
        AiNodeSetFlt(options, str::pixel_aspect_ratio, VtValueGetFloat(pixelAspectRatioValue));
    
    GfVec2i resolution; 
    if (!renderSettings.GetResolutionAttr().Get(&resolution, time.frame)) {
        // shouldn't happen, but if for some reason we can't access the render settings 
        // resolution, then we fallback to the current values in the options node (which
        // default to 320x240)
        resolution[0] = AiNodeGetInt(options, str::xres);
        resolution[1] = AiNodeGetInt(options, str::yres);
    }

    // Eventual render region: in arnold it's expected to be in pixels in the range [0, resolution]
    // but in usd it's between [0, 1]
    GfVec4f windowNDC;
    if (renderSettings.GetDataWindowNDCAttr().Get(&windowNDC, time.frame)) {
        // We want the output buffer to match the expected resolution. 
        // Therefore we need to adjust xres, yres, so that the region size equals
        // the expected resolution
        GfVec2i origResolution = resolution;
        // Need to invert the window range in the Y axis
        float minY = 1. - windowNDC[3];
        float maxY = 1. - windowNDC[1];
        windowNDC[1] = minY;
        windowNDC[3] = maxY;

        // Ensure the user isn't setting invalid ranges
        if (windowNDC[0] > windowNDC[2])
            std::swap(windowNDC[0], windowNDC[2]);
        if (windowNDC[1] > windowNDC[3])
            std::swap(windowNDC[1], windowNDC[3]);
        
        float xDelta = windowNDC[2] - windowNDC[0]; // maxX - minX
        if (xDelta > AI_EPSILON) {
            float xInvDelta = 1.f / xDelta;
            // adjust the X resolution accordingly
            resolution[0] *= xInvDelta;
            windowNDC[0] *= xInvDelta;
            windowNDC[2] *= xInvDelta;
        }

        float yDelta = windowNDC[3] - windowNDC[1]; // maxY - minY
        if (yDelta > AI_EPSILON) {
            float yInvDelta = 1.f / yDelta;
            // adjust the Y resolution accordingly
            resolution[1] *= yInvDelta;
            windowNDC[1] *= yInvDelta;
            windowNDC[3] *= yInvDelta;
            // need to adjust the pixel aspect ratio to match the window NDC
            float pixel_aspect_ratio = xDelta / yDelta;
            AiNodeSetFlt(options, str::pixel_aspect_ratio, pixel_aspect_ratio);
        }        
        AiNodeSetInt(options, str::region_min_x, int(windowNDC[0] * origResolution[0]));
        AiNodeSetInt(options, str::region_min_y, int(windowNDC[1] * origResolution[1]));
        AiNodeSetInt(options, str::region_max_x, int(windowNDC[2] * origResolution[0]) - 1);
        AiNodeSetInt(options, str::region_max_y, int(windowNDC[3] * origResolution[1]) - 1);
    }
    // image resolution : note that USD allows for different resolution per-AOV,
    // which is not possible in arnold
    AiNodeSetInt(options, str::xres, resolution[0]);
    AiNodeSetInt(options, str::yres, resolution[1]);
    
    
    // instantShutter will ignore any motion blur
    VtValue instantShutterValue;
    if (renderSettings.GetInstantaneousShutterAttr().Get(&instantShutterValue, time.frame) && 
            VtValueGetBool(instantShutterValue)) {
        AiNodeSetBool(options, str::ignore_motion_blur, true);
    }

    // Get the camera used for rendering, this is needed in arnold
    UsdRelationship cameraRel = renderSettings.GetCameraRel();
    SdfPathVector camTargets;
    cameraRel.GetTargets(&camTargets);
    UsdPrim camera;
    if (!camTargets.empty()) {
        camera = context.GetReader()->GetStage()->GetPrimAtPath(camTargets[0]);
        // just supporting a single camera for now
        if (camera)
            context.AddConnection(options, "camera", camera.GetPath().GetText(), UsdArnoldReader::CONNECTION_PTR);
    }

    std::vector<std::string> outputs;
    std::vector<std::string> lpes;
    std::vector<AtNode *> aovShaders;

    // Every render product is translated as an arnold driver.
    UsdRelationship productsRel = renderSettings.GetProductsRel();
    SdfPathVector productTargets;
    productsRel.GetTargets(&productTargets);
    for (size_t i = 0; i < productTargets.size(); ++i) {

        UsdPrim productPrim = context.GetReader()->GetStage()->GetPrimAtPath(productTargets[i]);
        UsdRenderProduct renderProduct(productPrim);
        if (!renderProduct) // couldn't find the render product in the usd scene
            continue;

        // The product name is supposed to return the output image filename
        VtValue productNameValue;
        std::string filename = renderProduct.GetProductNameAttr().Get(&productNameValue, time.frame) ?
            VtValueGetString(productNameValue, &prim) : std::string();
      
        if (filename.empty()) // no filename is provided, we can skip this product
            continue;

        // By default, we'll be saving out to exr
        std::string driverType = "driver_exr";
        std::string extension = TfGetExtension(filename);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        // Get the proper driver type based on the file extension
        if (extension == "tif")
            driverType = "driver_tiff";
        else if (extension == "jpg" || extension == "jpeg")
            driverType = "driver_jpeg";
        else if (extension == "png")
            driverType = "driver_png";
        else if (extension.empty()) // no extension provided, we'll save it as exr
            filename += std::string(".exr");

        // Create the driver for this render product
        AtNode *driver = context.CreateArnoldNode(driverType.c_str(), productPrim.GetPath().GetText());
        // Set the filename for the output image
        AiNodeSetStr(driver, str::filename, AtString(filename.c_str()));

        // Render Products have a list of Render Vars, which correspond to an AOV.
        // For each Render Var, we will need one element in options.outputs
        UsdRelationship renderVarsRel = renderProduct.GetOrderedVarsRel();
        SdfPathVector renderVarsTargets;
        renderVarsRel.GetTargets(&renderVarsTargets);
        
        // If, for the same driver, several AOVs have the same name, we need to give them a layer name.
        // We'll be verifying this during our loop
        bool useLayerName = false;
        std::vector<std::string> layerNames;
        std::unordered_set<std::string> aovNames;
        size_t prevOutputsCount = outputs.size();

        for (size_t j = 0; j < renderVarsTargets.size(); ++j) {

            UsdPrim renderVarPrim = context.GetReader()->GetStage()->GetPrimAtPath(renderVarsTargets[j]);
            if (!renderVarPrim || !renderVarPrim.IsActive())
                continue;
            UsdRenderVar renderVar(renderVarPrim);
            if (!renderVar) // couldn't find the renderVar in the usd scene
                continue;

            // We use a closest filter by default. Its name will be based on the renderVar name
            std::string filterName = renderVarPrim.GetPath().GetText() + std::string("/filter");
            std::string filterType = "closest_filter";
            
            // An eventual attribute "arnold:filter" will tell us what filter to create
            UsdAttribute filterAttr = renderVarPrim.GetAttribute(_tokens->aovSettingFilter);
            if (filterAttr) {
                VtValue filterValue;
                if (filterAttr.Get(&filterValue, time.frame))
                    filterType = VtValueGetString(filterValue, &prim);
            }

            // Create a filter node of the given type
            AtNode *filter = context.CreateArnoldNode(filterType.c_str(), filterName.c_str());
            
            // Set the filter width if the attribute exists in this filter type
            if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(filter), str::width)) {

                float filterWidth = 1.f;
                // An eventual attribute "arnold:width" will determine the filter width attribute
                UsdAttribute filterWidthAttr = renderVarPrim.GetAttribute(_tokens->aovSettingWidth);
                if (filterWidthAttr) {
                    VtValue filterWidthValue;
                    if (filterWidthAttr.Get(&filterWidthValue, time.frame))
                        filterWidth = VtValueGetFloat(filterWidthValue);
                }
                AiNodeSetFlt(filter, str::width, filterWidth);
            }

            // read attributes for a specific filter type, authored as "arnold:gaussian_filter:my_attr"
            std::string filterTypeAttrs = "arnold:";
            filterTypeAttrs += filterType;
            ReadArnoldParameters(renderVarPrim, context, filter, time, TfToken(filterTypeAttrs.c_str()));
            filterName = AiNodeGetName(filter);

            TfToken dataType;
            renderVar.GetDataTypeAttr().Get(&dataType, time.frame);
            const ArnoldAOVTypes arnoldTypes = _GetArnoldTypesFromTokenType(dataType);
            
            // Get the name for this AOV
            VtValue sourceNameValue;
            std::string sourceName = renderVar.GetSourceNameAttr().Get(&sourceNameValue, time.frame) ?
                VtValueGetString(sourceNameValue, &prim) : "RGBA";
            
            // The source Type will tell us if this AOV is a LPE, a primvar, etc...
            TfToken sourceType;
            renderVar.GetSourceTypeAttr().Get(&sourceType, time.frame);
                        
            std::string output;
            std::string aovName = sourceName;
            // Check if we already found this AOV name in the current driver
            if (aovNames.find(aovName) == aovNames.end()) {
                aovNames.insert(aovName);
            }
            else {
                // we found the same aov name multiple times, we'll need to add the layerName
                useLayerName = true;
            }
            VtValue aovNameValue;
            // read the parameter "driver:parameters:aov:name" that will be needed if we have merged exrs (see #816)
            std::string layerName = (renderVarPrim.GetAttribute(_tokens->aovSettingName).Get(&aovNameValue, time.frame)) ? 
                VtValueGetString(aovNameValue, &prim) : renderVarPrim.GetPath().GetName();

            if (sourceType == UsdRenderTokens->lpe) {
                // For Light Path Expressions, sourceName will return the expression.
                // The actual AOV name is eventually set in "driver:parameters:aov:name"
                // In arnold, we need to add an alias in options.light_path_expressions.
                aovName = layerName;
                lpes.push_back(aovName + std::string(" ") + sourceName);

            } else if (sourceType == UsdRenderTokens->primvar) {
                // Primvar AOVs are supposed to return the value of a primvar in the AOV.
                // This can be done in arnold with aov shaders, with a combination of
                // aov_write_*, and user_data_* nodes.

                // Create the aov_write shader, of the right type depending on the output AOV type
                std::string aovShaderName = renderVarPrim.GetPath().GetText() + std::string("/shader");
                AtNode *aovShader = context.CreateArnoldNode(arnoldTypes.aovWrite.c_str(), aovShaderName.c_str());
                // Set the name of the AOV that needs to be filled
                AiNodeSetStr(aovShader, str::aov_name, AtString(aovName.c_str()));

                // Create a user data shader that will read the desired primvar, its type depends on the AOV type
                std::string userDataName = renderVarPrim.GetPath().GetText() + std::string("/user_data");
                AtNode *userData = context.CreateArnoldNode(arnoldTypes.userData.c_str(), userDataName.c_str());
                // Link the user_data to the aov_write
                AiNodeLink(userData, "aov_input", aovShader);
                // Set the user data (primvar) to read
                AiNodeSetStr(userData, str::attribute, AtString(sourceName.c_str()));
                // We need to add the aov shaders to options.aov_shaders. 
                // Each of these shaders will be evaluated for every camera ray
                aovShaders.push_back(aovShader);
            }
            if (aovName.empty())
                continue; // No AOV name found, there's nothing we can do

            // Set the line to be added to options.outputs for this specific AOV
            output = aovName; // AOV name
            output += std::string(" ") + arnoldTypes.outputString; // AOV type (RGBA, VECTOR, etc..)
            output += std::string(" ") + filterName; // name of the filter for this AOV
            output += std::string(" ") + productPrim.GetPath().GetText(); // name of the driver for this AOV
                        
            // Add this output to the full list
            outputs.push_back(output);
            // also add the layer name in case we need to add it
            layerNames.push_back(layerName);
        }
        
        if (useLayerName) {
            // We need to distinguish several AOVs in this driver that have the same name, 
            // let's go through all of them and append the layer name to their output strings
            for (size_t j = 0; j < layerNames.size(); ++j) {
                outputs[j + prevOutputsCount] += std::string(" ") + layerNames[j];
            }
        }
    }
    // Set options.outputs, with all the AOVs to be rendered
    if (!outputs.empty()) {
        AtArray *outputsArray = AiArrayAllocate(outputs.size(), 1, AI_TYPE_STRING);
        for (size_t i = 0; i < outputs.size(); ++i)
            AiArraySetStr(outputsArray, i, AtString(outputs[i].c_str()));
        AiNodeSetArray(options, str::outputs, outputsArray);
    }
    // Set options.light_path_expressions with all the LPE aliases
    if (!lpes.empty()) {
        AtArray *lpesArray = AiArrayAllocate(lpes.size(), 1, AI_TYPE_STRING);
        for (size_t i = 0; i < lpes.size(); ++i)
            AiArraySetStr(lpesArray, i, AtString(lpes[i].c_str()));
        AiNodeSetArray(options, str::light_path_expressions, lpesArray);
    }
    // Set options.aov_shaders, will all the shaders to be evaluated
    if (!aovShaders.empty()) {
        AtArray *aovShadersArray = AiArrayAllocate(aovShaders.size(), 1, AI_TYPE_NODE);
        for (size_t i = 0; i < aovShaders.size(); ++i)
            AiArraySetPtr(aovShadersArray, i, (void*)aovShaders[i]);
        AiNodeSetArray(options, str::aov_shaders, aovShadersArray);
    }

    // There can be different namespaces for the arnold-specific attributes in the render settings node.
    // The usual namespace for any primitive (meshes, lights, etc...) is primvars:arnold
    ReadArnoldParameters(prim, context, options, time, "primvars:arnold");
    // For options, we can also look directly in the arnold: namespace
    ReadArnoldParameters(prim, context, options, time, "arnold");
    // Solaris is exporting arnold options in the arnold:global: namespace
    ReadArnoldParameters(prim, context, options, time, "arnold:global");

    // Read eventual connections to a node graph
    UsdArnoldNodeGraphConnection(options, prim, prim.GetAttribute(_tokens->aovGlobalAtmosphere), "atmosphere", context);
    UsdArnoldNodeGraphConnection(options, prim, prim.GetAttribute(_tokens->aovGlobalBackground), "background", context);
    UsdArnoldNodeGraphAovConnection(options, prim, prim.GetAttribute(_tokens->aovGlobalAovs), "aov_shaders", context);

    // Setup color manager
    AtNode* colorManager;
    const char *ocio_path = std::getenv("OCIO");
    if (ocio_path) {
        colorManager = AiNode(AiNodeGetUniverse(options), str::color_manager_ocio, str::color_manager_ocio);
        AiNodeSetPtr(options, str::color_manager, colorManager);
        AiNodeSetStr(colorManager, str::config, AtString(ocio_path));
    }
    else {
        // use the default color manager
        colorManager = AiNodeLookUpByName(AiNodeGetUniverse(options), str::ai_default_color_manager_ocio);
    }
    if (UsdAttribute colorSpaceLinearAttr = prim.GetAttribute(_tokens->colorSpaceLinear)) {
        VtValue colorSpaceLinearValue;
        if (colorSpaceLinearAttr.Get(&colorSpaceLinearValue, time.frame)) {
            std::string colorSpaceLinear = VtValueGetString(colorSpaceLinearValue, &prim);
            AiNodeSetStr(colorManager, str::color_space_linear, AtString(colorSpaceLinear.c_str()));
        }
    }
    if (UsdAttribute colorSpaceNarrowAttr = prim.GetAttribute(_tokens->colorSpaceNarrow)) {
        VtValue colorSpaceNarrowValue;
        if (colorSpaceNarrowAttr.Get(&colorSpaceNarrowValue, time.frame)) {
            std::string colorSpaceNarrow = VtValueGetString(colorSpaceNarrowValue, &prim);
            AiNodeSetStr(colorManager, str::color_space_narrow, AtString(colorSpaceNarrow.c_str()));
        }
    }

    // log file
    if (UsdAttribute logFileAttr = prim.GetAttribute(_tokens->logFile)) {
        VtValue logFileValue;
        if (logFileAttr.Get(&logFileValue, time.frame)) {
            std::string logFile = VtValueGetString(logFileValue, &prim);
            AiMsgSetLogFileName(logFile.c_str());
        }
    }

    // log verbosity
    if (UsdAttribute logVerbosityAttr = prim.GetAttribute(_tokens->logVerbosity)) {
        VtValue logVerbosityValue;
        if (logVerbosityAttr.Get(&logVerbosityValue, time.frame)) {
            int logVerbosity = ArnoldUsdGetLogVerbosityFromFlags(VtValueGetInt(logVerbosityValue));
            AiMsgSetConsoleFlags(AiNodeGetUniverse(options), logVerbosity);
            AiMsgSetLogFileFlags(AiNodeGetUniverse(options), logVerbosity);
        }
    }
}

