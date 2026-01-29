//
// SPDX-License-Identifier: Apache-2.0
//

#include <unordered_set>
#include <vector>
#include "rendersettings_utils.h"

#include "constant_strings.h"
#include "common_utils.h"
#include "parameters_utils.h"


#include <pxr/usd/usdRender/tokens.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/var.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usdGeom/camera.h>

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE


// THESE ARE COMING FROM read_options.cpp
// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((aovSettingFilter, "arnold:filter"))
    ((aovSettingWidth, "arnold:width"))
    ((aovSettingCamera, "arnold:camera"))
    ((aovFormat, "arnold:format"))
    ((aovDriver, "arnold:driver"))
    ((aovDriverFormat, "driver:parameters:aov:format"))
    ((aovSettingName,"driver:parameters:aov:name"))
    ((aovGlobalAtmosphere, "arnold:global:atmosphere"))
    ((aovGlobalBackground, "arnold:global:background"))
    ((aovGlobalImager, "arnold:global:imager"))
    ((aovGlobalShaderOverride, "arnold:global:shader_override"))
    ((aovGlobalAovs, "arnold:global:aov_shaders"))
    ((globalOperator, "arnold:global:operator"))
    ((colorSpaceLinear, "arnold:global:color_space_linear"))
    ((colorSpaceNarrow, "arnold:global:color_space_narrow"))
    ((colorManagerPrefix, "arnold:color_manager"))
    ((colorManagerEntry, "arnold:color_manager:node_entry"))
    ((logFile, "arnold:global:log:file"))
    ((logVerbosity, "arnold:global:log:verbosity"))
    ((reportFile, "arnold:global:report:file"))
    ((statsFile, "arnold:global:stats:file"))
    ((profileFile, "arnold:global:profile:file"))
    ((arnoldName, "arnold:name"))
    ((inputsName, "inputs:name"))
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
// clang-format on

ArnoldAOVTypes GetArnoldTypesFromFormatToken(const TfToken &type)
{
    // We check for the most common cases first.
    if (type == _tokens->color3f) {
        return {"RGB", str::aov_write_rgb, str::user_data_rgb, false};
    } else if (type == _tokens->color3h) {
        return {"RGB", str::aov_write_rgb, str::user_data_rgb, true};
    } else if (
        type == _tokens->float4 || type == _tokens->color4f || type == _tokens->color4u8 || type == _tokens->color4i8 ||
        type == _tokens->int4 || type == _tokens->uint4) {
        return {"RGBA", str::aov_write_rgba, str::user_data_rgba, false};
    } else if (type == _tokens->half4 || type == _tokens->color4h) {
        return {"RGBA", str::aov_write_rgba, str::user_data_rgba, true};
    } else if (type == _tokens->float3) {
        return {"VECTOR", str::aov_write_vector, str::user_data_rgb, false};
    } else if (type == _tokens->float2) {
        return {"VECTOR2", str::aov_write_vector, str::user_data_rgb, false};
    } else if (type == _tokens->half || type == _tokens->float16) {
        return {"FLOAT", str::aov_write_float, str::user_data_float, true};
    } else if (type == _tokens->_float) {
        return {"FLOAT", str::aov_write_float, str::user_data_float, false};
    } else if (type == _tokens->_int || type == _tokens->i8 || type == _tokens->uint8) {
        return {"INT", str::aov_write_int, str::user_data_int, false};
    } else if (type == _tokens->half2 || type == _tokens->color2h) {
        return {"VECTOR2", str::aov_write_vector, str::user_data_rgb, true};
    } else if (
        type == _tokens->color2f || type == _tokens->color2u8 || type == _tokens->color2i8 || type == _tokens->int2 ||
        type == _tokens->uint2) {
        return {"VECTOR2", str::aov_write_vector, str::user_data_rgb, false};
    } else if (type == _tokens->half3) {
        return {"VECTOR", str::aov_write_vector, str::user_data_rgb, true};
    } else if (type == _tokens->int3 || type == _tokens->uint3) {
        return {"VECTOR", str::aov_write_vector, str::user_data_rgb, false};
    } else {
        return {"RGB", str::aov_write_rgb, str::user_data_rgb, false};
    }
}

// Read eventual connections to a ArnoldNodeGraph primitive, that acts as a passthrough
static inline void UsdArnoldNodeGraphConnection(AtNode *node, const UsdPrim &prim, const UsdAttribute &attr,
                                                const std::string &attrName, ArnoldAPIAdapter &context, const TimeSettings &time)
{
    VtValue value;
    if (attr && attr.Get(&value, time.frame)) {
        // RenderSettings have a string attribute, referencing a prim in the stage
        std::string valStr = VtValueGetString(value);
        if (!valStr.empty()) {
            SdfPath path(valStr);
            // We check if there is a primitive at the path of this string
            UsdPrim ngPrim = prim.GetStage()->GetPrimAtPath(SdfPath(valStr));
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
                        UsdPrim outPrim = prim.GetStage()->GetPrimAtPath(outPath);
                        if (outPrim) {
                            std::string targetName = outPath.GetString();
                            // If the primitive linked by the node graph, has a "name" attribute, we need to check it here
                            // to eventually use it instead of the usd name
                            UsdAttribute nameAttr = outPrim.GetAttribute(
                                (outPrim.IsA<UsdShadeShader>()) ? _tokens->inputsName : _tokens->arnoldName);
                            if (nameAttr && nameAttr.HasAuthoredValue()) {
                                VtValue nameVal;
                                if (nameAttr.Get(&nameVal, time.frame)) {
                                    std::string nameStr = VtValueGetString(nameVal);
                                    if (!nameStr.empty())
                                        targetName = nameStr;
                                }
                            }
                            context.AddConnection(node, attrName, targetName.c_str(), ArnoldAPIAdapter::CONNECTION_PTR);
                        }
                    }
                }
            }
        }
    }
}


// Read eventual connections to a ArnoldNodeGraph primitive for the aov_shader shader array connections
static inline void UsdArnoldNodeGraphAovConnection(AtNode *options, const UsdPrim &prim, 
    const UsdAttribute &attr, const std::string &attrBase, ArnoldAPIAdapter &context, const TimeSettings &time)
{

    VtValue value;
    if (attr && attr.Get(&value, time.frame)) {
        // RenderSettings have a string attribute, referencing multiple prims in the stage
        std::string valStr = VtValueGetString(value);
        if (!valStr.empty()) {
            AtArray* aovShadersArray = AiNodeGetArray(options, str::aov_shaders);
            unsigned numElements = AiArrayGetNumElements(aovShadersArray);
            for(const auto &nodeGraphPrimName: TfStringTokenize(valStr)) {
                SdfPath nodeGraphPrimPath(nodeGraphPrimName);
                // We check if there is a primitive at the path of this string
                UsdPrim nodeGraphPrim = prim.GetStage()->GetPrimAtPath(nodeGraphPrimPath);

                if (nodeGraphPrim && nodeGraphPrim.GetTypeName() == _tokens->ArnoldNodeGraph) {
                    // We can use a UsdShadeShader schema in order to read connections
                    UsdShadeShader ngShader(nodeGraphPrim);
                    for (unsigned aovShaderIndex=1;; aovShaderIndex++) {
                        // the output terminal name will be aov_shader:i{1,...,n} as a contiguous array
                        TfToken outputName(attrBase + std::string(":i") + std::to_string(aovShaderIndex));
                        UsdShadeOutput outputAttr = ngShader.GetOutput(outputName);
                        if (!outputAttr) {
                            break;
                        }
                        SdfPathVector sourcePaths;
                        // Check which shader is connected to this output
                        if (outputAttr.HasConnectedSource() && outputAttr.GetRawConnectedSourcePaths(&sourcePaths)) {
                            for(const SdfPath &aovShaderPath : sourcePaths) {
                                SdfPath aovShaderPrimPath(aovShaderPath.GetPrimPath());
                                UsdPrim outPrim = prim.GetStage()->GetPrimAtPath(aovShaderPrimPath);
                                if (outPrim) {
                                    // we connect to aov_shaders{0,...,n-1} parameters i.e. 0 indexed, offset from any previous connections
                                    std::string optionAovShaderElement = attrBase + "[" + std::to_string(numElements++) + "]";
                                    context.AddConnection(options, optionAovShaderElement, aovShaderPrimPath.GetText(), ArnoldAPIAdapter::CONNECTION_PTR);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// Encapsulate the logic to extract driver type and settings from a UsdProduct prim
// The function can return nullptr if it wasn't able to find the driver
AtNode * ReadDriverFromRenderProduct(const UsdRenderProduct &renderProduct, ArnoldAPIAdapter &context, const TimeSettings &time) {
    // Driver type - We assume that the renderProduct has an attribute arnold:driver which contains the driver type
    UsdPrim renderProductPrim = renderProduct.GetPrim();
    UsdAttribute driverAttr = renderProductPrim.GetAttribute(_tokens->aovDriver);
    if (!driverAttr) return nullptr;
    std::string driverTypeName;
    driverAttr.Get<std::string>(&driverTypeName, UsdTimeCode(time.frame)); // Should we use VtValueGetString to be consistent ??
    AtNode *driver = context.CreateArnoldNode(driverTypeName.c_str(), renderProductPrim.GetPath().GetText());
    if (!driver) {
        return nullptr;
    }

    // The driver output filename is the usd RenderProduct name
    VtValue productNameValue;
    std::string filename = renderProduct.GetProductNameAttr().Get(&productNameValue, time.frame) ?
    VtValueGetString(productNameValue) : renderProductPrim.GetName().GetText();

    // Set the filename for the output image
    AiNodeSetStr(driver, str::filename, AtString(filename.c_str()));
    const std::string driverParamPrefix = "arnold:" + driverTypeName + ":";
    // All the attributes having the arnold:{driverType} prefix are the settings of the driver
    for (const UsdAttribute &attr: renderProductPrim.GetAttributes()) {        
        const std::string attrName = attr.GetName().GetString();
        if (TfStringStartsWith(attrName, driverParamPrefix)) {
            const std::string driverParamName = attrName.substr(driverParamPrefix.size());
            const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(driver), AtString(driverParamName.c_str()));
            if (!paramEntry) {
                continue;
            }
            // Driver's input is treated below, through a node graph connection
            if (driverParamName == "input")
                continue;

            const int paramType = AiParamGetType(paramEntry); 
            const int arrayType = AiParamGetSubType(paramEntry);
            ReadAttribute(attr, driver, driverParamName, time,
                           context, paramType, arrayType);
        }
    }

    // Read the color space for this driver
    if (UsdAttribute colorSpaceAttr = renderProductPrim.GetAttribute(str::t_arnold_color_space)) {
        VtValue colorSpaceValue;
        if (colorSpaceAttr.Get(&colorSpaceValue, UsdTimeCode(time.frame))) {
            const std::string colorSpaceStr = VtValueGetString(colorSpaceValue);
            AiNodeSetStr(driver, str::color_space, AtString(colorSpaceStr.c_str()));
        }
    }
    // Check if an imager is connected to this driver
    UsdArnoldNodeGraphConnection(driver, renderProductPrim, renderProductPrim.GetAttribute(TfToken(driverParamPrefix + std::string("input"))), "input", context, time);
    return driver;
}

AtNode * DeduceDriverFromFilename(const UsdRenderProduct &renderProduct, ArnoldAPIAdapter &context, const TimeSettings &time) {
        // The product name is supposed to return the output image filename.
        // If none is provided, we'll use the primitive name
    VtValue productNameValue;
    UsdPrim renderProductPrim = renderProduct.GetPrim();
    std::string filename = renderProductPrim.GetName().GetText();
    if (renderProduct.GetProductNameAttr().Get(&productNameValue, time.frame)) {
        std::string productName = VtValueGetString(productNameValue);
        if (!productName.empty())
            filename = productName;
    }

    // By default, we'll be saving out to exr
    std::string driverType = "driver_exr";
    std::string extension = TfGetExtension(filename);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    // Check if the render product type is deep
    VtValue productTypeValue;
    renderProduct.GetProductTypeAttr().Get(&productTypeValue, time.frame);
    if (productTypeValue != VtValue() && productTypeValue.Get<TfToken>()==TfToken("deep")) {
        driverType = "driver_deepexr";
    }

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
    AtNode *driver = context.CreateArnoldNode(driverType.c_str(), renderProductPrim.GetPath().GetText());
    // Set the filename for the output image
    AiNodeSetStr(driver, str::filename, AtString(filename.c_str()));

    // Read the driver attributes
    for (const UsdAttribute &attr: renderProductPrim.GetAttributes()) {
        const std::string arnoldPrefix = "arnold:";
        const std::string attrName = attr.GetName().GetString();
        if (TfStringStartsWith(attrName, arnoldPrefix)) {
            const std::string driverParamName = attrName.substr(arnoldPrefix.size());
            const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(driver), AtString(driverParamName.c_str()));
            if (!paramEntry) {
                continue;
            }
            const int paramType = AiParamGetType(paramEntry); 
            const int arrayType = AiParamGetSubType(paramEntry);
            ReadAttribute(attr, driver, driverParamName, time,
                        context, paramType, arrayType);
        }
    }
    return driver;
}

void ComputeUsdLuxVersion(
    UsdStageRefPtr _stage, const UsdPrim &options, TimeSettings &_time, const AtUniverse *universe)
{
    // Recuperate usdlux_version from render settings and send it to the core
    UsdAttribute usdlux_setting = options.GetAttribute(str::t_usdlux_setting);
    if (usdlux_setting && usdlux_setting.HasAuthoredValue()) {
        std::string usdluxName;
        usdlux_setting.Get(&usdluxName, _time.frame);
        AtNode *arnoldOptions = AiUniverseGetOptions(universe);
        AiNodeSetStr(arnoldOptions, str::usdlux_version, AtString(usdluxName.c_str()));
    }
}

// THIS IS NOT USED in the render delegate
void ComputeMotionRange(UsdStageRefPtr _stage, const UsdPrim &options,  TimeSettings &_time)
{
    UsdPrim cameraPrim;
    if (options.IsA<UsdRenderSettings>()) {
        UsdRenderSettings renderSettings(options);
        if (!renderSettings)
            return;
        // Get the camera used for rendering, this is needed 
        // to get the motion range to be used for the whole scene
        UsdRelationship cameraRel = renderSettings.GetCameraRel();
        SdfPathVector camTargets;
        cameraRel.GetTargets(&camTargets);
        if (!camTargets.empty()) {
            cameraPrim = _stage->GetPrimAtPath(camTargets[0]);
        }
            
    } else if (options.GetTypeName() == str::t_ArnoldOptions) {
        UsdAttribute cameraAttr = options.GetAttribute(str::t_arnold_camera);
        if (!cameraAttr)
            cameraAttr = options.GetAttribute(str::t_camera);
        if (cameraAttr) {
            std::string cameraName;
            cameraAttr.Get(&cameraName, _time.frame);
            if (!cameraName.empty())
                cameraPrim = _stage->GetPrimAtPath(SdfPath(cameraName.c_str()));
        }
    }

    if (cameraPrim) {
        UsdGeomCamera camera(cameraPrim);

        float shutterStart = 0.f;
        float shutterEnd = 0.f;

        if (camera) {

            VtValue shutterOpenValue;
            if (camera.GetShutterOpenAttr().Get(&shutterOpenValue, _time.frame)) {
                shutterStart = shutterOpenValue.Get<double>(); // might be wrong
            }
            VtValue shutterCloseValue;
            if (camera.GetShutterCloseAttr().Get(&shutterCloseValue, _time.frame)) {
                shutterEnd = shutterCloseValue.Get<double>(); // might be wrong
            }
        }
        _time.motionBlur = (shutterEnd > shutterStart);
        _time.motionStart = shutterStart;
        _time.motionEnd = shutterEnd;
    }
}

void ChooseRenderSettings(UsdStageRefPtr _stage, std::string &_renderSettings, TimeSettings &_time, UsdPrim *rootPrimPtr) {

    if (!_stage) return;

    // Simplest use case : the render settings name has been explicitely set.
    std::string optionsName = _renderSettings;
    
    // If not, we'll first search for a metadata called renderSettingsPrimPath on the stage
    // https://graphics.pixar.com/usd/release/api/usd_render_page_front.html
    if (optionsName.empty() && _stage->HasMetadata(UsdRenderTokens->renderSettingsPrimPath)) {
        VtValue renderSettingsPrimPath;
        _stage->GetMetadata(UsdRenderTokens->renderSettingsPrimPath, &renderSettingsPrimPath);
        optionsName = VtValueGetString(renderSettingsPrimPath);
    }

    // If not found, we'll search for a primitive called "options", which is the node name
    // in Arnold, and which is the name we author by default
    if (optionsName.empty())
        optionsName = "/options";

    UsdPrim options = _stage->GetPrimAtPath(SdfPath(optionsName));        
    if (options && (options.GetTypeName() == str::t_ArnoldOptions || options.IsA<UsdRenderSettings>())) {
        _renderSettings = optionsName;
        // TODO We should be able to compute the motion range after
        //ComputeMotionRange(_stage, options, _time);
    } else {
        if (rootPrimPtr == nullptr) {
            // By convention, the RenderSettings primitive should be under the "Render" scope.
            // We'll first try to find it under this primitive if it exists.
            UsdPrim renderPrim = _stage->GetPrimAtPath(SdfPath("/Render"));
            if (renderPrim) {
                UsdPrimRange range = UsdPrimRange(renderPrim);
                for (auto iter = range.begin(); iter != range.end(); ++iter) {
                    const UsdPrim &prim(*iter);
                    if (prim.IsA<UsdRenderSettings>()) {
                        _renderSettings = prim.GetPath().GetString();
                        //ComputeMotionRange(_stage, prim, _time);
                        break;
                    }
                }
            } else {
                // less efficient use case, we didn't find any options so far so we're going to 
                // traverse the whole stage, and stop at the first RenderSettings / ArnoldOptions primitive we find
                UsdPrimRange range = _stage->Traverse();
                for (auto iter = range.begin(); iter != range.end(); ++iter) {
                    const UsdPrim &prim(*iter);
                    if (prim.IsA<UsdRenderSettings>() || prim.GetTypeName() == str::t_ArnoldOptions) {
                        _renderSettings = prim.GetPath().GetString();
                        //ComputeMotionRange(_stage, prim, _time);
                        break;
                    }
                }
            }
        }
    }

}

void SetArnoldDefaultOptions(AtUniverse *universe)
{
    // Set default attribute values so that they match the defaults in arnold plugins,
    // as well as the render delegate's #1525
    AtNode *options = AiUniverseGetOptions(universe);
    AiNodeSetInt(options, str::AA_samples, 3);
    AiNodeSetInt(options, str::GI_diffuse_depth, 1);
    AiNodeSetInt(options, str::GI_specular_depth, 1);
    AiNodeSetInt(options, str::GI_transmission_depth, 8);
}

void SetRegion(AtNode* options, const GfVec4f& windowNDC, const GfVec2i& resolution)
{
    if ((!GfIsClose(windowNDC[0], 0.0f, AI_EPSILON)) || 
        (!GfIsClose(windowNDC[1], 0.0f, AI_EPSILON)) || 
        (!GfIsClose(windowNDC[2], 1.0f, AI_EPSILON)) || 
        (!GfIsClose(windowNDC[3], 1.0f, AI_EPSILON))) {
        // Need to invert the window range in the Y axis
        GfVec4f adjustedWindow = windowNDC;
        float minY = 1. - adjustedWindow[3];
        float maxY = 1. - adjustedWindow[1];
        adjustedWindow[1] = minY;
        adjustedWindow[3] = maxY;

        // Ensure the user isn't setting invalid ranges
        if (adjustedWindow[0] > adjustedWindow[2])
            std::swap(adjustedWindow[0], adjustedWindow[2]);
        if (adjustedWindow[1] > adjustedWindow[3])
            std::swap(adjustedWindow[1], adjustedWindow[3]);
        
        AiNodeSetInt(options, str::region_min_x, int(adjustedWindow[0] * resolution[0]));
        AiNodeSetInt(options, str::region_min_y, int(adjustedWindow[1] * resolution[1]));
        AiNodeSetInt(options, str::region_max_x, int(adjustedWindow[2] * resolution[0]) - 1);
        AiNodeSetInt(options, str::region_max_y, int(adjustedWindow[3] * resolution[1]) - 1);
    }
}

AtNode* GetOrCreateColorManager(const UsdPrim &renderSettingsPrim, ArnoldAPIAdapter &context, 
                                 const TimeSettings &time, AtNode *options)
{
    AtNode* colorManager = nullptr;
    const char *ocio_path = std::getenv("OCIO");
    if (ocio_path) {
        colorManager = context.CreateArnoldNode("color_manager_ocio", "color_manager_ocio");
        AiNodeSetStr(colorManager, str::config, AtString(ocio_path));
    }
    else if (UsdAttribute colorManagerEntryAttr = renderSettingsPrim.GetAttribute(_tokens->colorManagerEntry)) {
        VtValue colorManagerEntryValue;
        // if color_manager:node_entry is found, we want to create a color manager node of that given type
        if (colorManagerEntryAttr.Get(&colorManagerEntryValue, time.frame)) {
            std::string colorManagerEntry = VtValueGetString(colorManagerEntryValue);
            colorManager = context.CreateArnoldNode(colorManagerEntry.c_str(), colorManagerEntry.c_str());
        }
    }

    if (colorManager == nullptr) {
        // use the default color manager
        colorManager = AiNodeLookUpByName(AiNodeGetUniverse(options), str::ai_default_color_manager_ocio);
    }
    return colorManager;
}

void SetupColorManagerColorSpaces(AtNode *colorManager, const UsdPrim &renderSettingsPrim, 
                                   ArnoldAPIAdapter &context, const TimeSettings &time)
{
    if (!colorManager) {
        return;
    }

    // First we check the UsdRenderSettings builtin attribute renderingColorSpace, which can
    // define the attribute color_space_linear
#if PXR_VERSION >= 2211
    UsdRenderSettings renderSettings(renderSettingsPrim);
    if (renderSettings) {
        VtValue renderingSpaceValue;
        UsdAttribute renderingSpaceAttr = renderSettings.GetRenderingColorSpaceAttr();
        if (renderingSpaceAttr.HasAuthoredValue() && renderingSpaceAttr.Get(&renderingSpaceValue, time.frame)) {
            std::string renderingSpace = VtValueGetString(renderingSpaceValue);
            AiNodeSetStr(colorManager, str::color_space_linear, AtString(renderingSpace.c_str()));
        }
    }
#endif

    // Check for attributes "arnold:global:color_space_linear" and "arnold:global:color_space_narrow"
    // and set them in the color manager node
    if (UsdAttribute colorSpaceLinearAttr = renderSettingsPrim.GetAttribute(_tokens->colorSpaceLinear)) {
        VtValue colorSpaceLinearValue;
        if (colorSpaceLinearAttr.Get(&colorSpaceLinearValue, time.frame)) {
            std::string colorSpaceLinear = VtValueGetString(colorSpaceLinearValue);
            AiNodeSetStr(colorManager, str::color_space_linear, AtString(colorSpaceLinear.c_str()));
        }
    }
    if (UsdAttribute colorSpaceNarrowAttr = renderSettingsPrim.GetAttribute(_tokens->colorSpaceNarrow)) {
        VtValue colorSpaceNarrowValue;
        if (colorSpaceNarrowAttr.Get(&colorSpaceNarrowValue, time.frame)) {
            std::string colorSpaceNarrow = VtValueGetString(colorSpaceNarrowValue);
            AiNodeSetStr(colorManager, str::color_space_narrow, AtString(colorSpaceNarrow.c_str()));
        }
    }
    // Finally, loop over all the attributes namespaced with arnold:color_manager and set them in the 
    // color manager node
    ReadArnoldParameters(renderSettingsPrim, context, colorManager, time, "arnold:color_manager");
}

AtNode* ReadRenderSettings(const UsdPrim &renderSettingsPrim, ArnoldAPIAdapter &context, 
    ProceduralReader *reader, const TimeSettings &time, AtUniverse *universe, SdfPath& cameraPath) {

    AtNode *options = AiUniverseGetOptions(universe);
    auto stage = renderSettingsPrim.GetStage();
    UsdRenderSettings renderSettings(renderSettingsPrim);
    if (!renderSettings)
        return nullptr;

    VtValue pixelAspectRatioValue;
    if (renderSettings.GetPixelAspectRatioAttr().Get(&pixelAspectRatioValue, time.frame))
        AiNodeSetFlt(options, str::pixel_aspect_ratio, VtValueGetFloat(pixelAspectRatioValue));
    
    GfVec2i resolution; 
    if (renderSettings.GetResolutionAttr().Get(&resolution, time.frame)) {
        // image resolution : note that USD allows for different resolution per-AOV,
        // which is not possible in arnold
        AiNodeSetInt(options, str::xres, resolution[0]);
        AiNodeSetInt(options, str::yres, resolution[1]);        
    } else {
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
        SetRegion(options, windowNDC, resolution);
    }    
    
    // instantShutter will ignore any motion blur
    VtValue instantShutterValue;
    // GetInstantaneousShutterAttr is deprecated, we should use disableMotionBlur on the render product
    if (renderSettings.GetInstantaneousShutterAttr().Get(&instantShutterValue, time.frame) && 
            VtValueGetBool(instantShutterValue)) {
        AiNodeSetBool(options, str::ignore_motion_blur, true);
    }

    // Get the camera used for rendering, this is needed in arnold
    if (cameraPath.IsEmpty()) {
        UsdRelationship cameraRel = renderSettings.GetCameraRel();
        SdfPathVector camTargets;
        cameraRel.GetTargets(&camTargets);
        if (!camTargets.empty())
            cameraPath = camTargets[0];
    }
    UsdPrim camera = renderSettingsPrim.GetStage()->GetPrimAtPath(cameraPath);
    // just supporting a single camera for now
    if (camera)
        context.AddConnection(options, "camera", camera.GetPath().GetText(), ArnoldAPIAdapter::CONNECTION_PTR);
    
    std::vector<std::string> outputs;
    std::vector<std::string> lpes;
    std::vector<AtNode *> aovShaders;
    // collect beauty drivers from beauty outputs across all products, use a set as there be multiple
    std::set<AtNode *> beautyDrivers;

    // Every render product is translated as an arnold driver.
    UsdRelationship productsRel = renderSettings.GetProductsRel();
    SdfPathVector productTargets;
    productsRel.GetTargets(&productTargets);
    for (size_t i = 0; i < productTargets.size(); ++i) {

        UsdPrim productPrim = renderSettingsPrim.GetStage()->GetPrimAtPath(productTargets[i]);
        UsdRenderProduct renderProduct(productPrim);
        if (!renderProduct) // couldn't find the render product in the usd scene
            continue;

        AtNode *driver = nullptr;
        if (HasAuthoredAttribute(productPrim, _tokens->aovDriver)) {
            driver = ReadDriverFromRenderProduct(renderProduct, context, time);
        } else {
            driver = DeduceDriverFromFilename(renderProduct, context, time);
        }
        if (!driver) {
            continue;
        }
        std::string driverName = AiNodeGetName(driver);
        // Needed further down
        const std::string driverType(AiNodeEntryGetName(AiNodeGetNodeEntry(driver)));

        // Set imager in the driver
        UsdArnoldNodeGraphConnection(driver, renderSettingsPrim, renderSettingsPrim.GetAttribute(_tokens->aovGlobalImager), "input", context, time);

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
        std::unordered_set<std::string> duplicatedAovs;
        std::vector<std::string> aovNamesList;
        size_t prevOutputsCount = outputs.size();
        std::vector<bool> isHalfList;
        bool isDriverExr = AiNodeIs(driver, str::driver_exr);
        for (size_t j = 0; j < renderVarsTargets.size(); ++j) {

            UsdPrim renderVarPrim = renderSettingsPrim.GetStage()->GetPrimAtPath(renderVarsTargets[j]);
            if (!renderVarPrim || !renderVarPrim.IsActive())
                continue;
            UsdRenderVar renderVar(renderVarPrim);
            if (!renderVar) // couldn't find the renderVar in the usd scene
                continue;

            // We use a closest filter by default. Its name will be based on the renderVar name
            std::string filterName = renderVarPrim.GetPath().GetText() + std::string("/filter");
            std::string filterType = "box_filter";
            
            // An eventual attribute "arnold:filter" will tell us what filter to create
            UsdAttribute filterAttr = renderVarPrim.GetAttribute(_tokens->aovSettingFilter);
            if (filterAttr) {
                VtValue filterValue;
                if (filterAttr.Get(&filterValue, time.frame)) {
                    filterType = VtValueGetString(filterValue);
                }
            }

            // Create a filter node of the given type
            AtNode *filter = AiNodeLookUpByName(universe, AtString(filterName.c_str()));
            if (filter == nullptr)
                filter = context.CreateArnoldNode(filterType.c_str(), filterName.c_str());
            
            // Set the filter width if the attribute exists in this filter type
            if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(filter), str::width)) {
                // An eventual attribute "arnold:width" will determine the filter width attribute
                UsdAttribute filterWidthAttr = renderVarPrim.GetAttribute(_tokens->aovSettingWidth);
                VtValue filterWidthValue;
                if (filterWidthAttr && filterWidthAttr.Get(&filterWidthValue, time.frame)) {
                    AiNodeSetFlt(filter, str::width, VtValueGetFloat(filterWidthValue));
                }
            }

            // read attributes for a specific filter type, authored as "arnold:gaussian_filter:my_attr"
            std::string filterTypeAttrs = "arnold:";
            filterTypeAttrs += filterType;
            ReadArnoldParameters(renderVarPrim, context, filter, time, TfToken(filterTypeAttrs.c_str()));
            filterName = AiNodeGetName(filter);

            TfToken dataType;
            renderVar.GetDataTypeAttr().Get(&dataType, time.frame);

            // override with the driver:parameters:aov:format
            if (UsdAttribute aovDriverFormatAttr = renderVarPrim.GetAttribute(_tokens->aovDriverFormat)) {
                aovDriverFormatAttr.Get(&dataType, time.frame);
            }

            // If the attribute arnold:format is present, it overrides the dataType attr
            // (this is needed for cryptomatte in Hydra #1164)
            if (UsdAttribute arnoldFormatAttr = renderVarPrim.GetAttribute(_tokens->aovFormat)) {
                arnoldFormatAttr.Get(&dataType, time.frame);
            }
            const ArnoldAOVTypes arnoldTypes = GetArnoldTypesFromFormatToken(dataType);
            
            // Get the name for this AOV
            VtValue sourceNameValue;
            std::string sourceName = renderVar.GetSourceNameAttr().Get(&sourceNameValue, time.frame) ?
                VtValueGetString(sourceNameValue) : "RGBA";

            // we want to consider "color" as referring to the beauty, just like "RGBA" (see #1311)
            if (sourceName == "color")
                sourceName = "RGBA";

            // The source Type will tell us if this AOV is a LPE, a primvar, etc...
            TfToken sourceType;
            renderVar.GetSourceTypeAttr().Get(&sourceType, time.frame);
            
            VtValue aovNameValue;
            std::string layerName = renderVarPrim.GetPath().GetName();
            bool hasLayerName = false;

            // read the parameter "driver:parameters:aov:name" that will be needed if we have merged exrs (see #816)
            if (renderVarPrim.GetAttribute(_tokens->aovSettingName).Get(&aovNameValue, time.frame)) {
                std::string aovNameValueStr = VtValueGetString(aovNameValue);
                if (!aovNameValueStr.empty()) {
                    layerName = aovNameValueStr;
                    hasLayerName = true;
                }
            }
            // optional per-AOV camera
            std::string cameraName;
            VtValue cameraValue;
            if (renderVarPrim.GetAttribute(_tokens->aovSettingCamera).Get(&cameraValue, time.frame)) {
                cameraName = VtValueGetString(cameraValue);
            }            
            
            std::string aovName = sourceName;
            
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
                AtNode *aovShader = context.CreateArnoldNode(arnoldTypes.aovWrite, aovShaderName.c_str());
                // Set the name of the AOV that needs to be filled
                AiNodeSetStr(aovShader, str::aov_name, AtString(aovName.c_str()));

                // Create a user data shader that will read the desired primvar, its type depends on the AOV type
                std::string userDataName = renderVarPrim.GetPath().GetText() + std::string("/user_data");
                AtNode *userData = context.CreateArnoldNode(arnoldTypes.userData, userDataName.c_str());
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

            
            bool isDuplicatedAov = (hasLayerName && aovName != layerName);
            // Check if we already found this AOV name in the current driver
            if (aovNames.find(sourceName) == aovNames.end()) {
                aovNames.insert(sourceName);
            }
            else {
                isDuplicatedAov = true;
            }
            if (isDuplicatedAov) {
                // we found the same aov name multiple times, we'll need to add the layerName
                useLayerName = true;
                // store the list of aov names that were actually duplicated
                duplicatedAovs.insert(sourceName);
            }
            
            // Set the line to be added to options.outputs for this specific AOV
            std::string output;
            if (!cameraName.empty())
                output = cameraName + std::string(" ");
            output += aovName; // AOV name
            output += std::string(" ") + arnoldTypes.outputString; // AOV type (RGBA, VECTOR, etc..)
            output += std::string(" ") + filterName; // name of the filter for this AOV
            output += std::string(" ") + driverName; // name of the driver for this AOV

            // Track beauty outputs drivers
            if (aovName == "RGBA")
                beautyDrivers.insert(driver);

            // Add this output to the full list
            outputs.push_back(output);
            // also add the layer name in case we need to add it
            layerNames.push_back(layerName);
            // Finally, store the source name of the AOV for this output. 
            // We'll use it to recognize if this AOV is duplicated or not
            aovNamesList.push_back(sourceName);
            // Remember if this output is half precision or not
            isHalfList.push_back(isDriverExr ? arnoldTypes.isHalf : false);
        } // End renderVar loop
        
        if (useLayerName) {
            // We need to distinguish several AOVs in this driver that have the same name, 
            // let's go through all of them and append the layer name to their output strings

            for (size_t j = 0; j < layerNames.size(); ++j) {
                // We only add the layer name if this AOV has been found several time
                if (duplicatedAovs.find(aovNamesList[j]) == duplicatedAovs.end())
                    continue;

                outputs[j + prevOutputsCount] += std::string(" ") + layerNames[j];
            }
        }
        // For exr drivers, we need to set the attribute "half_precision"
        if (!isHalfList.empty()) {
            bool isHalfDriver = true;
            // we'll consider that this driver_exr needs half precision if
            // all AOVs are half precision
            for (size_t j = 0; j < isHalfList.size(); ++j) {
                if (isHalfList[j]) {
                    outputs[j + prevOutputsCount] += " HALF";
                } else {
                    isHalfDriver = false;
                }
            }
            // We only want to force it to true if all AOVs are half precision. 
            // But this can still be enabled from the driver parameters
            // so we don't want to disable it here
            if (isHalfDriver && driverType == "driver_exr")
                AiNodeSetBool(driver, AtString("half_precision"), true);
        }
    } // End renderProduct loop

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
    ReadArnoldParameters(renderSettingsPrim, context, options, time, "primvars:arnold");
    // For options, we can also look directly in the arnold: namespace
    ReadArnoldParameters(renderSettingsPrim, context, options, time, "arnold");
    // Solaris is exporting arnold options in the arnold:global: namespace
    ReadArnoldParameters(renderSettingsPrim, context, options, time, "arnold:global");

    // Read eventual connections to a node graph
    UsdArnoldNodeGraphConnection(options, renderSettingsPrim, renderSettingsPrim.GetAttribute(_tokens->aovGlobalAtmosphere), "atmosphere", context, time);
    UsdArnoldNodeGraphConnection(options, renderSettingsPrim, renderSettingsPrim.GetAttribute(_tokens->aovGlobalBackground), "background", context, time);
    UsdArnoldNodeGraphConnection(options, renderSettingsPrim, renderSettingsPrim.GetAttribute(_tokens->aovGlobalShaderOverride), "shader_override", context, time);
    UsdArnoldNodeGraphAovConnection(options, renderSettingsPrim, renderSettingsPrim.GetAttribute(_tokens->aovGlobalAovs), "aov_shaders", context, time);

    UsdArnoldNodeGraphConnection(options, renderSettingsPrim, renderSettingsPrim.GetAttribute(_tokens->globalOperator), "operator", context, time);

    // Setup color manager
    AtNode* colorManager = GetOrCreateColorManager(renderSettingsPrim, context, time, options);
    if (colorManager) {
        // Set the color manager node in the options
        AiNodeSetPtr(options, str::color_manager, colorManager);
        // Set color space attributes on the color manager
        SetupColorManagerColorSpaces(colorManager, renderSettingsPrim, context, time);
    }

    const std::string &commandLine = reader->GetCommandLine();
    // log file
    if (UsdAttribute logFileAttr = renderSettingsPrim.GetAttribute(_tokens->logFile)) {
        VtValue logFileValue;
        // We only read this attribute if it wasn't explicitely set in the command line
        if (commandLine.find(" -logfile ") == std::string::npos && logFileAttr.Get(&logFileValue, time.frame)) {
            const std::string logFile = VtValueGetString(logFileValue);
            AiMsgSetLogFileName(logFile.c_str());
        }
    }

    // log verbosity
    if (UsdAttribute logVerbosityAttr = renderSettingsPrim.GetAttribute(_tokens->logVerbosity)) {
        VtValue logVerbosityValue;
        // We only read this attribute if it wasn't explicitely set in the command line
        if (commandLine.find(" -v ") == std::string::npos && logVerbosityAttr.Get(&logVerbosityValue, time.frame)) {
            int logVerbosity = ArnoldUsdGetLogVerbosityFromFlags(VtValueGetInt(logVerbosityValue));
#if ARNOLD_VERSION_NUM >= 70100
            AiMsgSetConsoleFlags(AiNodeGetUniverse(options), logVerbosity);
            AiMsgSetLogFileFlags(AiNodeGetUniverse(options), logVerbosity);
#else
            AiMsgSetConsoleFlags(nullptr, logVerbosity);
            AiMsgSetLogFileFlags(nullptr, logVerbosity);
#endif
        }
    }

    // html report file
#if ARNOLD_VERSION_NUM >= 70401
    if (UsdAttribute reportFileAttr = renderSettingsPrim.GetAttribute(_tokens->reportFile)) {
        VtValue reportFileValue;
        // We only read this attribute if it's not explicitely set in the command line
        if (commandLine.find(" -report ") == std::string::npos && reportFileAttr.Get(&reportFileValue, time.frame)) {
            const std::string reportFile = VtValueGetString(reportFileValue);
            AiReportSetFileName(reportFile.c_str());
        }
    }
#endif
    // stats file
    if (UsdAttribute statsFileAttr = renderSettingsPrim.GetAttribute(_tokens->statsFile)) {
        VtValue statsFileValue;
        // We only read this attribute if it's not explicitely set in the command line
        if (commandLine.find(" -statsfile ") == std::string::npos && statsFileAttr.Get(&statsFileValue, time.frame)) {
            const std::string statsFile = VtValueGetString(statsFileValue);
            AiStatsSetFileName(statsFile.c_str());
        }
    }

    // profile file
    if (UsdAttribute profileFileAttr = renderSettingsPrim.GetAttribute(_tokens->profileFile)) {
        VtValue profileFileValue;
        // We only read this attribute if it's not explicitely set in the command line
        if (commandLine.find(" -profile ") == std::string::npos && profileFileAttr.Get(&profileFileValue, time.frame)) {
            const std::string profileFile = VtValueGetString(profileFileValue);
            AiProfileSetFileName(profileFile.c_str());
        }
    }
    
    return options;
}

PXR_NAMESPACE_CLOSE_SCOPE
