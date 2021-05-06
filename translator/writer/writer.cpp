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
#include "writer.h"

#include <ai.h>

#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xform.h>

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
     (startFrame)
     (endFrame)
);

// global writer registry, will be used in the default case
static UsdArnoldWriterRegistry *s_writerRegistry = nullptr;

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

    AtNode *camera = AiUniverseGetCamera(universe);
    if (camera) {
        _shutterStart = AiNodeGetFlt(camera, AtString("shutter_start"));
        _shutterEnd = AiNodeGetFlt(camera, AtString("shutter_end"));
    }

    // If a specific time was requested, we want to check if some data was already written 
    // to this USD stage for other frames. We do this by checking the options node, as its attribute
    // "frame" will contain the list of frames
    if (!_time.IsDefault()) {
        _mask |= AI_NODE_OPTIONS; // we always need the options written out if a time was provided, to store the frame
        _authoredFrames.clear();
        _nearestFrames.clear();
        
        float currentFrame = (float) _time.GetValue();
    
        // we also want to set startFrame, endFrame in the stage metadata
        float startFrame = currentFrame;
        float endFrame = currentFrame;

        std::string optionsName = 
            UsdArnoldPrimWriter::GetArnoldNodeName(AiUniverseGetOptions(universe), *this);
        
        // Find the options primitive that was eventually authored previously
        UsdPrim optionsPrim = _stage->GetPrimAtPath(SdfPath(optionsName.c_str()));
        if (optionsPrim) {
            UsdAttribute frames = optionsPrim.GetAttribute(_tokens->frame);
            if (frames) {
                // There is already an options node with some values in "frame",
                // we get the list of time samples for it.
                std::vector<double> timeSamples;
                frames.GetTimeSamples(&timeSamples);

                // if we don't have any time sample, or if we just have one equal to the current frame,
                // then we don't need to look for previously authored frames.
                if (!timeSamples.empty() &&
                             (timeSamples.size() > 1 || currentFrame != (float)timeSamples[0])) {

                    _authoredFrames = std::vector<float>(timeSamples.begin(), timeSamples.end());

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
            }
        }
        _stage->SetMetadata(_tokens->startFrame, (double)startFrame);
        _stage->SetMetadata(_tokens->endFrame, (double)endFrame);        
    }

    // Loop over the universe nodes, and write each of them
    AtNodeIterator *iter = AiUniverseGetNodeIterator(_universe, _mask);
    while (!AiNodeIteratorFinished(iter)) {
        WritePrimitive(AiNodeIteratorGetNext(iter));
    }
    AiNodeIteratorDestroy(iter);
    _universe = nullptr;
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

    AtString nodeName = AtString(AiNodeGetName(node));

    static const AtString rootStr("root");
    static const AtString ai_default_reflection_shaderStr("ai_default_reflection_shader");

    // some Arnold nodes shouldn't be saved
    if (nodeName == rootStr || nodeName == ai_default_reflection_shaderStr) {
        return;
    }

    // Check if this arnold node has already been exported, and early out if it was.
    // Note that we're storing the name of the arnold node, which might be slightly
    // different from the USD prim name, since UsdArnoldPrimWriter::GetArnoldNodeName
    // replaces some forbidden characters by underscores.
    if (!nodeName.empty() && IsNodeExported(nodeName))
        return;

    if (!nodeName.empty())
        _exportedNodes.insert(nodeName); // remember that we already exported this node

    std::string objType = AiNodeEntryGetName(AiNodeGetNodeEntry(node));
    UsdArnoldPrimWriter *primWriter = _registry->GetPrimWriter(objType);
    if (primWriter)
        primWriter->WriteNode(node, *this);
}

void UsdArnoldWriter::SetRegistry(UsdArnoldWriterRegistry *registry) { _registry = registry; }

void UsdArnoldWriter::CreateHierarchy(const SdfPath &path, bool leaf) const
{
    if (path == SdfPath::AbsoluteRootPath())
        return;
    
    if (!leaf) {
        // If this primitive was already written, let's early out.
        // No need to test this for the leaf node that is about 
        // to be created
        if (_stage->GetPrimAtPath(path))
            return;
    }

    // Ensure the parents xform are created first, otherwise they'll
    // be created implicitely without any type
    CreateHierarchy(path.GetParentPath(), false);

    // Finally, create the current non-leaf prim as a xform
    if (!leaf) 
        UsdGeomXform::Define(_stage, path);
}
