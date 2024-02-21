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
#include "writer.h"

#include <ai.h>

#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/base/vt/dictionary.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "prim_writer.h"
#include "registry.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
     ((frame, "arnold:frame"))
     (timeCodeArray)
     (startFrame)
     (endFrame)
);

// global writer registry, will be used in the default case
static UsdArnoldWriterRegistry *s_writerRegistry = nullptr;

static const SdfPath s_renderScope("/Render");
static const SdfPath s_renderProductsScope("/Render/Products");
static const SdfPath s_renderVarsScope("/Render/Vars");
/**
 *  Write out a given Arnold universe to a USD stage.
 **/
void UsdArnoldWriter::Write(const AtUniverse *universe)
{
    _universe = universe;
    // eventually use a dedicated registry
    if (_registry == nullptr) {
        // No registry was set (default), let's use the global one
        if (s_writerRegistry == nullptr) {
            s_writerRegistry = new UsdArnoldWriterRegistry(_writeBuiltin); // initialize the global registry
        }
        _registry = s_writerRegistry;
    }
    // clear the list of nodes that were exported to usd
    _exportedNodes.clear();

    // If we've explicitely set a scope, we want to use it as a defaultPrim
    if (_defaultPrim.empty() && !_scope.empty())
        _defaultPrim = _scope;
    
    AtNode *camera = AiUniverseGetCamera(universe);
    if (camera) {
        _shutterStart = AiNodeGetFlt(camera, AtString("shutter_start"));
        _shutterEnd = AiNodeGetFlt(camera, AtString("shutter_end"));
    }

    AtNode *options = AiUniverseGetOptions(universe);
    if (options) {
        const float fps = AiNodeGetFlt(options, AtString("fps"));
        _stage->GetRootLayer()->SetFramesPerSecond(static_cast<double>(fps));
        _stage->GetRootLayer()->SetTimeCodesPerSecond(static_cast<double>(fps));
    }

    // If a specific time was requested, we want to check if some data was already written 
    // to this USD stage for other frames. We do this by checking the scene custom metadata
    // "timeCodeArray" , that will contain the list of frames
    if (!_time.IsDefault()) {
        _authoredFrames.clear();
        _nearestFrames.clear();
        
        float currentFrame = (float) _time.GetValue();
    
        // we also want to set startFrame, endFrame in the stage metadata
        float startFrame = currentFrame;
        float endFrame = currentFrame;

        VtDictionary customLayerDataDict = _stage->GetRootLayer()->GetCustomLayerData();
        VtArray<SdfTimeCode> timeCodeArray;
        // Get the previously authored timeCodeArray, if present
        if (customLayerDataDict.find(_tokens->timeCodeArray) != customLayerDataDict.end()) {
            VtValue timeCodeVal = customLayerDataDict[_tokens->timeCodeArray];
            timeCodeArray = timeCodeVal.Get<VtArray<SdfTimeCode>>();
        }

        // if we don't have any time sample, or if we just have one equal to the current frame,
        // then we don't need to look for previously authored frames.
        if (!timeCodeArray.empty() &&
                     (timeCodeArray.size() > 1 || currentFrame != (float)timeCodeArray[0].GetValue())) {

            _authoredFrames.reserve(timeCodeArray.size());
            for (const auto &timeCode : timeCodeArray)
                _authoredFrames.push_back(timeCode.GetValue());
                        
            // Now, based on the list of previously authored frames, we want to find 
            // the nearest "surrounding" frames (lower and/or upper).
            // If a constant attribute becomes time varying, we will need to set 
            // time samples on these nearest frames.                     
            UsdTimeCode lowerFrame = UsdTimeCode::Default();
            UsdTimeCode upperFrame = UsdTimeCode::Default();
            for (auto frame : _authoredFrames) {
                if (frame < currentFrame && (lowerFrame.IsDefault() || frame > lowerFrame.GetValue()))
                    lowerFrame = UsdTimeCode(frame);
                else if (frame > currentFrame && (upperFrame.IsDefault() || frame < upperFrame.GetValue()))
                    lowerFrame = UsdTimeCode(frame);

                startFrame = std::min(frame, startFrame);
                endFrame = std::max(frame, endFrame);
            }
            // _nearestFrames should have one or two elements, representing the surrounding frames.
            if (!lowerFrame.IsDefault())
                _nearestFrames.push_back(lowerFrame.GetValue());
            if (!upperFrame.IsDefault())
                _nearestFrames.push_back(upperFrame.GetValue());                    
        }
        // Add the current frame to the list of authored frames, and set it
        // back to the custom layer data dictionary
        timeCodeArray.push_back(SdfTimeCode(currentFrame));
        customLayerDataDict[_tokens->timeCodeArray] = VtValue(timeCodeArray);
        _stage->GetRootLayer()->SetCustomLayerData(customLayerDataDict);

        _stage->SetMetadata(_tokens->startFrame, (double)startFrame);
        _stage->SetMetadata(_tokens->endFrame, (double)endFrame);        
    }

    // Loop over the universe nodes, and write each of them. We first want to write all node
    // except shaders. Those assigned to geometries will be exported during the process, 
    // with a given material's scope (#1067)
    AtNodeIterator *iter = AiUniverseGetNodeIterator(_universe, _mask  & ~AI_NODE_SHADER );
    while (!AiNodeIteratorFinished(iter)) {
        WritePrimitive(AiNodeIteratorGetNext(iter));
    }
    AiNodeIteratorDestroy(iter);

    // Then, do a second loop only through shaders in the arnold universe.
    // Those that weren't exported yet in the previous step, and that aren't 
    // therefore assigned to any geometry, will be exported here 
    if (_mask & AI_NODE_SHADER) {
        iter = AiUniverseGetNodeIterator(_universe, AI_NODE_SHADER );
        while (!AiNodeIteratorFinished(iter)) {
            AtNode *node = AiNodeIteratorGetNext(iter);
            // check if the shader was previously exported, i.e if it's
            // part of a shading tree assigned to a geometry
            if (_exportedShaders.find(node) != _exportedShaders.end())
                continue;
            WritePrimitive(node);
        }
        AiNodeIteratorDestroy(iter);
            
    }
    
    _universe = nullptr;

    // Set the defaultPrim in the current stage (#1063)
    if (!_defaultPrim.empty()) {
        // as explained in the USD API, the defaultPrim is not a path but a name,
        // so it shouldn't start with a slash
        if (_defaultPrim[0] == '/')
            _defaultPrim = _defaultPrim.substr(1);
        _stage->GetRootLayer()->SetDefaultPrim(TfToken(_defaultPrim.c_str()));
    }
}

/**
 *  Write out the primitive, by using the registered primitive writer.
 *
 **/
void UsdArnoldWriter::WritePrimitive(const AtNode *node)
{
    if (node == nullptr) {
        return;
    }

    std::string nodeName(AiNodeGetName(node));
    
    static const std::string rootStr("root");
    static const std::string ai_default_reflection_shaderStr("ai_default_reflection_shader");
    static const std::string ai_default_color_managerStr("ai_default_color_manager_ocio");

    // some Arnold nodes shouldn't be saved
    if (nodeName == rootStr || nodeName == ai_default_reflection_shaderStr || nodeName == ai_default_color_managerStr) {
        return;
    }
    if (!_scope.empty())
        nodeName = _scope + nodeName;

    // Check if this arnold node has already been exported, and early out if it was.
    // Note that we're storing the name of the arnold node, which might be slightly
    // different from the USD prim name, since UsdArnoldPrimWriter::GetArnoldNodeName
    // replaces some forbidden characters by underscores.
    // Note that we don't really need to take into account the "strip hierarchy" here
    // since we're testing the arnold node name    
    if (!nodeName.empty()) {
        if (IsNodeExported(nodeName))
            return;
        _exportedNodes.insert(nodeName); // remember that we already exported this node
    }

    std::string objType = AiNodeEntryGetName(AiNodeGetNodeEntry(node));
    UsdArnoldPrimWriter *primWriter = _registry->GetPrimWriter(objType);
    if (primWriter)
        primWriter->WriteNode(node, *this);
}

void UsdArnoldWriter::SetRegistry(UsdArnoldWriterRegistry *registry) { _registry = registry; }

void UsdArnoldWriter::CreateScopeHierarchy(const SdfPath &path)
{
    if (path == SdfPath::AbsoluteRootPath() || _stage->GetPrimAtPath(path))
        return;
        
    // Ensure the parents scopes are created first, otherwise they'll
    // be created implicitely without any type
    CreateScopeHierarchy(path.GetParentPath());
    UsdGeomScope::Define(_stage, path);
}

void UsdArnoldWriter::CreateHierarchy(const SdfPath &path, bool leaf)
{
    if (path == SdfPath::AbsoluteRootPath())
        return;
    
    if (!leaf) {
        // If this primitive was already written, let's early out.
        // No need to test this for the leaf node that is about 
        // to be created
        if (_stage->GetPrimAtPath(path)) {
            if (_defaultPrim.empty())
                _defaultPrim = path.GetText();

            return;
        }
    }

    // Ensure the parents xform are created first, otherwise they'll
    // be created implicitely without any type
    CreateHierarchy(path.GetParentPath(), false);

    // Finally, create the current non-leaf prim as a xform
    if (!leaf)
        UsdGeomXform::Define(_stage, path);
    
    // If no defaultPrim was previously set, set it now.
    if (_defaultPrim.empty())
        _defaultPrim = path.GetText();
}
const SdfPath &UsdArnoldWriter::GetRenderScope()
{    
    return s_renderScope;
}
const SdfPath &UsdArnoldWriter::GetRenderProductsScope()
{
    return s_renderProductsScope;
}
const SdfPath &UsdArnoldWriter::GetRenderVarsScope()
{
    return s_renderVarsScope;
}
