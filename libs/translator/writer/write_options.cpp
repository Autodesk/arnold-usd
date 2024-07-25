//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Autodesk, Inc.
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
#include "write_options.h"

#include <ai.h>

#include <pxr/base/gf/camera.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/var.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "registry.h"
#include "constant_strings.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((aovSettingFilter, "arnold:filter"))
    ((aovSettingWidth, "arnold:width"))
    ((aovSettingCamera, "arnold:camera"))
    ((aovFormat, "arnold:format"))
    ((aovDriver, "arnold:driver"))
    ((aovColorSpace, "arnold:color_space"))
    ((aaSamples, "arnold:AA_samples"))
    ((giDiffuseDepth, "arnold:GI_diffuse_depth"))
    ((giSpecularDepth, "arnold:GI_specular_depth"))
    ((aovDriverFormat, "driver:parameters:aov:format"))
    ((aovSettingName,"driver:parameters:aov:name"))
    ((aovGlobalAtmosphere, "arnold:global:atmosphere"))
    ((aovGlobalBackground, "arnold:global:background"))
    ((aovGlobalImager, "arnold:global:imager"))
    ((aovGlobalAovs, "arnold:global:aov_shaders"))
    ((colorManagerEntry, "arnold:color_manager:node_entry"))
    ((colorSpaceLinear, "arnold:global:color_space_linear"))
    ((colorSpaceNarrow, "arnold:global:color_space_narrow"))
    ((logFile, "arnold:global:log:file"))
    ((logVerbosity, "arnold:global:log:verbosity"))
    ((outputsInput, "outputs:input"))
    ((outputsBackground, "outputs:background"))
    (ArnoldNodeGraph)
    (Scope)
    ((_int, "int"))
    ((_float, "float"))
    (half) 
    (float2) (float3) (float4)
    (half2) (half3) (half4)
    (color2f) (color3f) (color4f)
    (color2h) (color3h) (color4h)
    ((_string, "string"))    
    );
// clang-format on

// Function to create an ArnoldNodeGraph for a given options attribute
static void _CreateNodeGraph(UsdPrim& prim, const AtNode* node, const AtString& attr,  
    UsdArnoldWriter &writer)
{
    std::vector<AtNode*> nodesArray; // list of connected nodes

    // Get the arnold attribute type
    const AtParamEntry *paramEntry = 
        AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(node), attr);
    int attrType = AiParamGetType(paramEntry);

    if (attrType == AI_TYPE_NODE) {
        // Node attribute: if a node is referenced, we add it to our list
        AtNode *target = (AtNode*)AiNodeGetPtr(node, attr);
        if (target == nullptr)
            return;
        nodesArray.push_back(target);
    } else if (attrType == AI_TYPE_ARRAY) {
        // Array attribute : we add each of the nodes to our list
        AtArray* array = AiNodeGetArray(node, attr);
        int numElements = array ? AiArrayGetNumElements(array) : 0;
        if (numElements == 0)
            return;
        nodesArray.resize(numElements);
        for (int i = 0; i < numElements; ++i) {
            nodesArray[i] = (AtNode*) AiArrayGetPtr(array, i);
        }
    }
    std::string attrStr(attr.c_str());
    static const std::string arnoldPrefix("arnold:global:");
    static const std::string graphBasename("/nodeGraph");
    static const std::string outputPrefix("outputs:");
    const std::string mtlScope = writer.GetMtlScope() + std::string("/");

    // The node graphs will go under the materials scope (/mtl by default)
    SdfPath scope(mtlScope + attrStr);
    writer.CreateScopeHierarchy(scope);
    UsdStageRefPtr stage = writer.GetUsdStage();

    // Get the previous writer scope to restore it at the end of this function
    std::string prevScope = writer.GetScope();

    // Name of the nodegraph, e.g. /mtl/background/nodeGraph
    std::string nodeGraphName = mtlScope + attrStr + graphBasename;
    // Set the nodeGraph path as a scope, so that the shaders we'll create below
    // go under its hierarchy
    writer.SetScope(nodeGraphName); 
    const std::string stripHierarchy = writer.GetStripHierarchy();

    // Create the ArnoldNodeGraph primitive
    UsdPrim nodeGraphPrim = stage->DefinePrim(SdfPath(nodeGraphName), _tokens->ArnoldNodeGraph);

    // Reference the nodeGraph in our RenderSetting's attribute (e.g. arnold:global:background)
    TfToken terminal(arnoldPrefix + attrStr);
    UsdAttribute nodeGraphTerminal = 
        prim.CreateAttribute(terminal, SdfValueTypeNames->String, false);
    nodeGraphTerminal.Set(nodeGraphName);

    std::string idSuffix;
    // Loop through each of the nodes to write
    for (size_t i = 0; i < nodesArray.size(); ++i) {
        AtNode* target = nodesArray[i];
        if (target == nullptr)
            continue;
        std::string targetName = UsdArnoldPrimWriter::GetArnoldNodeName(target, writer);

        std::string hierarchyPath = TfGetPathName(targetName);
        if (hierarchyPath != "/")
            writer.SetStripHierarchy(hierarchyPath);
        
        // Author the target shader, under the nodeGraph scope
        writer.WritePrimitive(target);
        UsdPrim targetPrim = stage->GetPrimAtPath(SdfPath(targetName));
        
        // For array attributes (aov_shaders) we need add the index, starting at 1
        // e.g. outputs:aov_shaders:i1
        if (attrType == AI_TYPE_ARRAY)
            idSuffix = TfStringPrintf(":i%d", i+1);
        
        // Create the node graph terminal
        TfToken outputGraphAttr(outputPrefix + attrStr + idSuffix);
        UsdAttribute nodeGraphAttr = nodeGraphPrim.CreateAttribute(outputGraphAttr, 
            SdfValueTypeNames->Token, false);

        // Ensure the target shader has an output attribute (outputs:out)
        UsdAttribute targetOutputAttr = targetPrim.CreateAttribute(str::t_outputs_out, SdfValueTypeNames->Token, false);
        SdfPath targetOutput(targetName + std::string(".outputs:out"));
        // Connect the node graph terminal to the target shader
        nodeGraphAttr.AddConnection(targetOutput);
        // Eventually restore the previous stripHierarchy
        if (hierarchyPath != "/")
            writer.SetStripHierarchy(stripHierarchy);
    }
    // Restore the previous scope
    writer.SetScope(prevScope);
}

static TfToken _GetUsdDataType(const std::string& aovType, bool isHalf)
{
    if (aovType == "RGB")
        return isHalf ? _tokens->color3h : _tokens->color3f;
    if (aovType == "RGBA")
        return isHalf ? _tokens->color4h : _tokens->color4f;
    if (aovType == "VECTOR")
        return isHalf ? _tokens->half3 : _tokens->float3;
    if (aovType == "VECTOR2")
        return isHalf ? _tokens->half2 : _tokens->float2;
    if (aovType == "FLOAT")
        return isHalf ? _tokens->half : _tokens->_float;
    if (aovType == "INT" || aovType == "BOOLEAN" || aovType == "BYTE" || aovType == "UINT")
        return _tokens->_int;
    if (aovType == "STRING")
        return _tokens->_string;

    return TfToken(aovType.c_str());
}

void UsdArnoldWriteOptions::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    const AtUniverse* universe = writer.GetUniverse();
    std::string nodeName = GetArnoldNodeName(node, writer); // This will return /Render/settings
    SdfPath renderScope("/Render");
    writer.CreateScopeHierarchy(renderScope);
    SdfPath objPath(nodeName);    
    UsdRenderSettings renderSettings = UsdRenderSettings::Define(stage, objPath);
    UsdPrim prim = renderSettings.GetPrim();

    writer.SetAttribute(renderSettings.CreatePixelAspectRatioAttr(), AiNodeGetFlt(node, str::pixel_aspect_ratio));
    _exportedAttrs.insert("pixel_aspect_ratio");

    GfVec2i resolution(AiNodeGetInt(node, str::xres), AiNodeGetInt(node, str::yres));
    writer.SetAttribute(renderSettings.CreateResolutionAttr(), resolution);
    _exportedAttrs.insert("xres");
    _exportedAttrs.insert("yres");

    GfVec4f cropRegion(AiNodeGetInt(node, str::region_min_x),
                       AiNodeGetInt(node, str::region_min_y),
                       AiNodeGetInt(node, str::region_max_x),
                       AiNodeGetInt(node, str::region_max_y));

    if (cropRegion[0] > 0 && cropRegion[1] > 0 && 
        cropRegion[2] > 0 && cropRegion[3] > 0) {
        cropRegion[0] /= resolution[0];
        cropRegion[1] /= resolution[1];
        cropRegion[2] = (cropRegion[2] + 1.f) / resolution[0];
        cropRegion[3] = (cropRegion[3] + 1.f) / resolution[1];

        cropRegion[1] = 1.f - cropRegion[1];
        cropRegion[3] = 1.f - cropRegion[3];

        writer.SetAttribute(renderSettings.CreateDataWindowNDCAttr(), cropRegion);
    }
    _exportedAttrs.insert("region_min_x");
    _exportedAttrs.insert("region_min_y");
    _exportedAttrs.insert("region_max_x");
    _exportedAttrs.insert("region_max_y");

    if (AiNodeGetBool(node, str::ignore_motion_blur)) {
        writer.SetAttribute(renderSettings.CreateInstantaneousShutterAttr(), true);
    }
    _exportedAttrs.insert("ignore_motion_blur");
    AtNode* camera = (AtNode*) AiNodeGetPtr(node, str::camera);
    if (camera) {
        writer.WritePrimitive(camera); // ensure the camera is written first
        std::string cameraName = UsdArnoldPrimWriter::GetArnoldNodeName(camera, writer);
        renderSettings.CreateCameraRel().AddTarget(SdfPath(cameraName));
    }
    std::string prevScope = writer.GetScope();
    writer.SetScope("");

    _exportedAttrs.insert("camera");
    // outputs will be handled below
    _exportedAttrs.insert("outputs");

    // The following attributes have a different default in Arnold core than in 
    // plugins, so we always want to author them
    writer.SetAttribute(
        prim.CreateAttribute(_tokens->aaSamples, SdfValueTypeNames->Int), 
        AiNodeGetInt(node, str::AA_samples));
    _exportedAttrs.insert("AA_samples");

    writer.SetAttribute(
        prim.CreateAttribute(_tokens->giDiffuseDepth, SdfValueTypeNames->Int), 
        AiNodeGetInt(node, str::GI_diffuse_depth));
    _exportedAttrs.insert("GI_diffuse_depth");

    writer.SetAttribute(
        prim.CreateAttribute(_tokens->giSpecularDepth, SdfValueTypeNames->Int), 
        AiNodeGetInt(node, str::GI_specular_depth));
    _exportedAttrs.insert("GI_specular_depth");

    AtNode* colorManager = (AtNode*) AiNodeGetPtr(node, str::color_manager);
    // If the options node has a color manager set, we want to author it in the render settings #1965
    if (colorManager) {
        const AtNodeEntry* cmEntry = AiNodeGetNodeEntry(colorManager);
        // write the node entry of the connected color manager node
        writer.SetAttribute(
            prim.CreateAttribute(_tokens->colorManagerEntry, SdfValueTypeNames->String), 
            AiNodeEntryGetName(cmEntry));

        // write the color manager attributes with the namespace "arnold:color_manager"
        _WriteArnoldParameters(colorManager, writer, prim, "arnold:color_manager");
        // Also author the rendering color space attribute which 
        // exists in UsdRenderSettings since USD 22.11
#if PXR_VERSION >= 2211
        AtString renderingSpace = AiNodeGetStr(colorManager, str::color_space_linear);
        TfToken renderingSpaceToken(renderingSpace.c_str());
        writer.SetAttribute(renderSettings.CreateRenderingColorSpaceAttr(), renderingSpaceToken);
#endif
    }
    _exportedAttrs.insert("color_manager");

    _CreateNodeGraph(prim, node, str::background, writer);
    _exportedAttrs.insert("background");

    _CreateNodeGraph(prim, node, str::atmosphere, writer);
    _exportedAttrs.insert("atmosphere");

    _CreateNodeGraph(prim, node, str::aov_shaders, writer);
    _exportedAttrs.insert("aov_shaders");

    // write the remaining Arnold attributes with the arnold: namespace    
    _WriteArnoldParameters(node, writer, prim, "arnold");

    AtArray *outputs = AiNodeGetArray(node, str::outputs);
    unsigned int numOutputs = outputs ? AiArrayGetNumElements(outputs) : 0;
    
    UsdRelationship productsList = renderSettings.CreateProductsRel();
    std::unordered_set<AtNode*> drivers;
    std::unordered_set<std::string> aovNames;

    if (numOutputs > 0) {
        writer.CreateScopeHierarchy(writer.GetRenderVarsScope());
        const std::string renderVarsPrefix = writer.GetRenderVarsScope().GetString() + std::string("/");

        for (unsigned int i = 0; i < numOutputs; ++i) {
            AtString outputAtStr = AiArrayGetStr(outputs, i);
            if (outputAtStr.empty())
                continue;
            std::string outputStr(outputAtStr.c_str());

            std::vector<std::string> tokens;
            std::stringstream f(outputAtStr.c_str());
            std::string s;    
            while (std::getline(f, s, ' ')) {
                tokens.push_back(s);
            }

            if (tokens.size() < 4) {
                // not enough tokens in the output string
                continue;
            }
            
            const AtNode* camNode = AiNodeLookUpByName(universe, AtString(tokens[0].c_str()));
            bool hasCamera = camNode != nullptr && tokens.size() >= 5 &&
                AiNodeEntryGetType(AiNodeGetNodeEntry(camNode)) == AI_NODE_CAMERA;
            size_t tokenIndex = 0;

            std::string camera;
            if (hasCamera) {
                camera = tokens[tokenIndex++];
            }
            
            std::string aovName = tokens[tokenIndex++];
            std::string aovType = tokens[tokenIndex++];
            
            AtNode* filter = AiNodeLookUpByName(universe, AtString(tokens[tokenIndex++].c_str()));
            if (filter == nullptr)
                continue;
            
            std::string driverName = tokens[tokenIndex++];
            AtNode* driver = AiNodeLookUpByName(universe, AtString(driverName.c_str()));
            if (driver == nullptr)
                continue;

            std::string layerName;
            bool isHalf = false;

            if(tokens.size() > tokenIndex) {
                // there are still remaining tokens
                // it can either be 
                if (tokens[tokenIndex] == "HALF") {
                    isHalf = true;
                } else {
                    layerName = tokens[tokenIndex++];
                    if (tokens.size() > tokenIndex && tokens[tokenIndex] == "HALF")
                        isHalf = true;

                }
            }

            // Create the RenderVar for this AOV
            std::string varName = renderVarsPrefix + aovName;
            int aovIndex = 0;
            while (aovNames.find(varName) != aovNames.end()) {
                varName = renderVarsPrefix + aovName + std::to_string(++aovIndex);
            }
            // Now we'll author a new prim for Render Vars,
            // let's clear _exportedAttrs so that it doesn't
            // conflict between different nodes
            _exportedAttrs.clear();

            aovNames.insert(varName);
            SdfPath aovPath(varName);
            UsdRenderVar renderVar = UsdRenderVar::Define(stage, aovPath);
            UsdPrim renderVarPrim = renderVar.GetPrim();
            writer.SetAttribute(
                renderVar.CreateSourceNameAttr(), aovName);
            const TfToken usdDataType = _GetUsdDataType(aovType, isHalf);
            writer.SetAttribute(
                renderVar.CreateDataTypeAttr(), usdDataType);
            // sourceType will tell us if this AOVs is a LPE, a primvar, etc...

            if (!layerName.empty()) {
                writer.SetAttribute(
                    renderVarPrim.CreateAttribute(_tokens->aovSettingName, SdfValueTypeNames->String), layerName);
            }
            if (!camera.empty()) {
                writer.SetAttribute(
                    renderVarPrim.CreateAttribute(_tokens->aovSettingCamera, SdfValueTypeNames->String), camera);
            }

            std::string filterType = AiNodeEntryGetName(AiNodeGetNodeEntry(filter));
            writer.SetAttribute(
                renderVarPrim.CreateAttribute(_tokens->aovSettingFilter, SdfValueTypeNames->String),
                filterType);

            // We always author the width as arnold:width
            if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(filter), str::width)) {
                writer.SetAttribute(
                    renderVarPrim.CreateAttribute(_tokens->aovSettingWidth, SdfValueTypeNames->Float),
                    AiNodeGetFlt(filter, str::width));
            }
            // Author the filter attributes with the arnold:{filterType}: prefix
            std::string filterAttrPrefix = std::string("arnold:") + filterType;
            _WriteArnoldParameters(filter, writer, renderVarPrim, filterAttrPrefix);


            // Ensure the render product is authored
            writer.WritePrimitive(driver);
            const SdfPath driverPath(GetArnoldNodeName(driver, writer).c_str());
            
            if (drivers.find(driver) == drivers.end()) {
                // First AOV using this driver, let's add it to the 
                // products list
                productsList.AddTarget(driverPath);
                drivers.insert(driver);
            }
            UsdRenderProduct renderProduct(stage->GetPrimAtPath(driverPath));
            if (renderProduct) {
                renderProduct.GetOrderedVarsRel().AddTarget(aovPath);
            }
        }
    }
    writer.SetScope(prevScope);
}

void UsdArnoldWriteDriver::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    writer.CreateScopeHierarchy(writer.GetRenderProductsScope());
    std::string prevScope = writer.GetScope();
    writer.SetScope("");

    UsdStageRefPtr stage = writer.GetUsdStage(); // Get the USD stage defined in the writer
    const AtUniverse* universe = writer.GetUniverse();
    std::string driverName = GetArnoldNodeName(node, writer); // This will return /Render/settings
    const SdfPath driverPath(driverName.c_str());

    UsdRenderProduct renderProduct = UsdRenderProduct::Define(stage, driverPath);
    UsdPrim renderProductPrim = renderProduct.GetPrim();

    const AtNodeEntry* driverEntry = AiNodeGetNodeEntry(node);
    std::string driverType = AiNodeEntryGetName(driverEntry);
    writer.SetAttribute(renderProductPrim.CreateAttribute(_tokens->aovDriver, SdfValueTypeNames->String),
        driverType);
    AtString filename = AiNodeGetStr(node, str::filename);
    _exportedAttrs.insert("filename");
    writer.SetAttribute(renderProduct.CreateProductNameAttr(), TfToken(filename.c_str()));
    renderProduct.CreateOrderedVarsRel();
    // loop through driver attributes, set them in the render product with the driverType prefix
   std::string attrPrefix = std::string("arnold:") + driverType;
   
   // FIXME add color space as arnold:color_space
   AtString colorSpace = AiNodeGetStr(node, str::color_space);
   if (!colorSpace.empty()) {
        writer.SetAttribute(
            renderProductPrim.CreateAttribute(_tokens->aovColorSpace, SdfValueTypeNames->String),
            std::string(colorSpace.c_str())); 
   }
        
    
   _WriteArnoldParameters(node, writer, renderProductPrim, attrPrefix);
   writer.SetScope(prevScope);

}