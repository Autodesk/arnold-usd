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

#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/var.h>
#include <pxr/base/tf/pathUtils.h>

#include "registry.h"
#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((aovSettingFilter, "arnold:filter"))
    ((aovSettingWidth, "arnold:width"))
    ((aovSettingName,"driver:parameters:aov:name"))
    ((_float, "float"))
    ((_int, "int"))
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

void UsdArnoldReadRenderSettings::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    // No need to create any node in arnold, since the options node is automatically created
    AtNode *options = AiUniverseGetOptions(context.GetReader()->GetUniverse());
    const TimeSettings &time = context.GetTimeSettings();

    UsdRenderSettings renderSettings(prim);
    if (!renderSettings)
        return;

    // image resolution : note that USD allows for different resolution per-AOV,
    // which is not possible in arnold
    GfVec2i resolution; 
    if (renderSettings.GetResolutionAttr().Get(&resolution)) {
        AiNodeSetInt(options, "xres", resolution[0]);
        AiNodeSetInt(options, "yres", resolution[1]);
    }
    VtValue pixelAspectRatioValue;
    if (renderSettings.GetPixelAspectRatioAttr().Get(&pixelAspectRatioValue))
        AiNodeSetFlt(options, "pixel_aspect_ratio", VtValueGetFloat(pixelAspectRatioValue));
    
    // Eventual render region: in arnold it's expected to be in pixels in the range [0, resolution]
    // but in usd it's between [0, 1]
    GfVec4f window;
    if (renderSettings.GetDataWindowNDCAttr().Get(&window)) {
        AiNodeSetInt(options, "region_min_x", int(window[0] * resolution[0]));
        AiNodeSetInt(options, "region_min_y", int(window[1] * resolution[1]));
        AiNodeSetInt(options, "region_max_x", int(window[2] * resolution[0]));
        AiNodeSetInt(options, "region_max_y", int(window[3] * resolution[1]));
    }
    
    // instantShutter will ignore any motion blur
    VtValue instantShutterValue;
    if (renderSettings.GetInstantaneousShutterAttr().Get(&instantShutterValue) && 
            VtValueGetBool(instantShutterValue)) {
        AiNodeSetBool(options, "ignore_motion_blur", true);
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
            context.AddConnection(options, "camera", camera.GetPath().GetText(), UsdArnoldReaderContext::CONNECTION_PTR);
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
        std::string filename = renderProduct.GetProductNameAttr().Get(&productNameValue) ?
            VtValueGetString(productNameValue) : std::string();
      
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
        AiNodeSetStr(driver, "filename", filename.c_str());

        // Render Products have a list of Render Vars, which correspond to an AOV.
        // For each Render Var, we will need one element in options.outputs
        UsdRelationship renderVarsRel = renderProduct.GetOrderedVarsRel();
        SdfPathVector renderVarsTargets;
        renderVarsRel.GetTargets(&renderVarsTargets);

        // if there are multiple renderVars for this driver, we'll need
        // to set the layerName (assuming the file format supports multilayer files)
        bool useLayerName = (renderVarsTargets.size() > 1);

        for (size_t j = 0; j < renderVarsTargets.size(); ++j) {

            UsdPrim renderVarPrim = context.GetReader()->GetStage()->GetPrimAtPath(renderVarsTargets[j]);
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
                if (filterAttr.Get(&filterValue))
                    filterType = VtValueGetString(filterValue);
            }

            // Create a filter node of the given type
            AtNode *filter = context.CreateArnoldNode(filterType.c_str(), filterName.c_str());
            
            // Set the filter width if the attribute exists in this filter type
            if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(filter), AtString("width"))) {

                float filterWidth = 1.f;
                // An eventual attribute "arnold:width" will determine the filter width attribute
                UsdAttribute filterWidthAttr = renderVarPrim.GetAttribute(_tokens->aovSettingWidth);
                if (filterWidthAttr) {
                    VtValue filterWidthValue;
                    if (filterWidthAttr.Get(&filterWidthValue))
                        filterWidth = VtValueGetFloat(filterWidthValue);
                }
                AiNodeSetFlt(filter, "width", filterWidth);
            }

            TfToken dataType;
            renderVar.GetDataTypeAttr().Get(&dataType);
            const ArnoldAOVTypes arnoldTypes = _GetArnoldTypesFromTokenType(dataType);
            
            // Get the name for this AOV
            VtValue sourceNameValue;
            std::string sourceName = renderVar.GetSourceNameAttr().Get(&sourceNameValue) ?
                VtValueGetString(sourceNameValue) : "RGBA";
            
            // The source Type will tell us if this AOV is a LPE, a primvar, etc...
            TfToken sourceType;
            renderVar.GetSourceTypeAttr().Get(&sourceType);
                        
            std::string output;
            std::string aovName = sourceName;

            if (sourceType == UsdRenderTokens->lpe) {
                // For Light Path Expressions, sourceName will return the expression.
                // The actual AOV name is eventually set in "driver:parameters:aov:name"
                // In arnold, we need to add an alias in options.light_path_expressions.
                VtValue aovNameValue;
                aovName = renderVarPrim.GetAttribute(_tokens->aovSettingName).Get(&aovNameValue) ?
                            VtValueGetString(aovNameValue) : renderVarPrim.GetPath().GetName();
                lpes.push_back(aovName + std::string(" ") + sourceName);

            } else if (sourceType == UsdRenderTokens->primvar) {
                // Primvar AOVs are supposed to return the value of a primvar in the AOV.
                // This can be done in arnold with aov shaders, with a combination of
                // aov_write_*, and user_data_* nodes.

                // Create the aov_write shader, of the right type depending on the output AOV type
                std::string aovShaderName = renderVarPrim.GetPath().GetText() + std::string("/shader");
                AtNode *aovShader = context.CreateArnoldNode(arnoldTypes.aovWrite.c_str(), aovShaderName.c_str());
                // Set the name of the AOV that needs to be filled
                AiNodeSetStr(aovShader, "aov_name", aovName.c_str());

                // Create a user data shader that will read the desired primvar, its type depends on the AOV type
                std::string userDataName = renderVarPrim.GetPath().GetText() + std::string("/user_data");
                AtNode *userData = context.CreateArnoldNode(arnoldTypes.userData.c_str(), userDataName.c_str());
                // Link the user_data to the aov_write
                AiNodeLink(userData, "aov_input", aovShader);
                // Set the user data (primvar) to read
                AiNodeSetStr(userData, "attribute", sourceName.c_str());
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
            
            // if multiple AOVs are saved to the same file/driver, we need to give them a layer name
            if (useLayerName)
                output += std::string(" ") + aovName;

            // Add this output to the full list
            outputs.push_back(output);
        }
    }

    // Set options.outputs, with all the AOVs to be rendered
    if (!outputs.empty()) {
        AtArray *outputsArray = AiArrayAllocate(outputs.size(), 1, AI_TYPE_STRING);
        for (size_t i = 0; i < outputs.size(); ++i)
            AiArraySetStr(outputsArray, i, AtString(outputs[i].c_str()));
        AiNodeSetArray(options, "outputs", outputsArray);
    }
    // Set options.light_path_expressions with all the LPE aliases
    if (!lpes.empty()) {
        AtArray *lpesArray = AiArrayAllocate(lpes.size(), 1, AI_TYPE_STRING);
        for (size_t i = 0; i < lpes.size(); ++i)
            AiArraySetStr(lpesArray, i, AtString(lpes[i].c_str()));
        AiNodeSetArray(options, "light_path_expressions", lpesArray);
    }
    // Set options.aov_shaders, will all the shaders to be evaluated
    if (!aovShaders.empty()) {
        AtArray *aovShadersArray = AiArrayAllocate(aovShaders.size(), 1, AI_TYPE_NODE);
        for (size_t i = 0; i < aovShaders.size(); ++i)
            AiArraySetPtr(aovShadersArray, i, (void*)aovShaders[i]);
        AiNodeSetArray(options, "aov_shaders", aovShadersArray);
    }

    // There can be different namespaces for the arnold-specific attributes in the render settings node.
    // The usual namespace for any primitive (meshes, lights, etc...) is primvars:arnold
    _ReadArnoldParameters(prim, context, options, time, "primvars:arnold");
    // For options, we can also look directly in the arnold: namespace
    _ReadArnoldParameters(prim, context, options, time, "arnold");
    // Solaris is exporting arnold options in the arnold:global: namespace
    _ReadArnoldParameters(prim, context, options, time, "arnold:global");

}
