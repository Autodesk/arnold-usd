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
static UsdArnoldWriterRegistry *s_writerRegistry = NULL;

/**
 *  Write out a given Arnold universe to a USD stage
 *
 **/
void UsdArnoldWriter::write(const AtUniverse *universe)
{
    _universe = universe;
    // eventually use a dedicated registry
    if (_registry == NULL) {
        // No registry was set (default), let's use the global one
        if (s_writerRegistry == NULL) {
            s_writerRegistry = new UsdArnoldWriterRegistry(_writeBuiltin); // initialize the global registry
        }
        _registry = s_writerRegistry;
    }
    // clear the list of nodes that were exported to usd
    _exportedNodes.clear(); 

    // Loop over the universe nodes, and write each of them
    AtNodeIterator *iter = AiUniverseGetNodeIterator(_universe, AI_NODE_ALL);
    while (!AiNodeIteratorFinished(iter)) {
        writePrimitive(AiNodeIteratorGetNext(iter));
    }
    AiNodeIteratorDestroy(iter);
    _universe = NULL;
}

/**
 *  Write out the primitive, by using the registered primitive writer.
 *
 **/
void UsdArnoldWriter::writePrimitive(const AtNode *node)
{
    if (node == NULL) {
        return;
    }

    AtString nodeName = AtString(AiNodeGetName(node));

    static const AtString rootStr("root");
    static const AtString ai_default_reflection_shaderStr("ai_default_reflection_shader");

    // some Arnold nodes shouldn't be saved
    if (nodeName == rootStr || nodeName == ai_default_reflection_shaderStr) {
        return;
    }

    if (isNodeExported(nodeName))
        return; // this node has already been exported, nothing to do

    std::string objType = AiNodeEntryGetName(AiNodeGetNodeEntry(node));

    UsdArnoldPrimWriter *primWriter = _registry->getPrimWriter(objType);
    if (primWriter) {
        _exportedNodes.insert(nodeName); // remember that we already exported this node
        primWriter->writeNode(node, *this);
    }
}

void UsdArnoldWriter::setRegistry(UsdArnoldWriterRegistry *registry) { _registry = registry; }
