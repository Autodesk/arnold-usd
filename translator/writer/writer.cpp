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

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "prim_writer.h"
#include "registry.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// global writer registry, will be used in the default case
static UsdArnoldWriterRegistry *s_writerRegistry = nullptr;

/**
 *  Write out a given Arnold universe to a USD stage
 *
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
    if (IsNodeExported(nodeName))
        return;

    std::string objType = AiNodeEntryGetName(AiNodeGetNodeEntry(node));

    UsdArnoldPrimWriter *primWriter = _registry->GetPrimWriter(objType);
    if (primWriter) {
        _exportedNodes.insert(nodeName); // remember that we already exported this node
        primWriter->WriteNode(node, *this);
    }
}

void UsdArnoldWriter::SetRegistry(UsdArnoldWriterRegistry *registry) { _registry = registry; }
