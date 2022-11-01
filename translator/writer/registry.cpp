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
#include "registry.h"

#include <ai.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "write_arnold_type.h"
#include "write_camera.h"
#include "write_geometry.h"
#include "write_light.h"
#include "write_shader.h"

#include <common_utils.h>

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// For now we're not registering any writer
UsdArnoldWriterRegistry::UsdArnoldWriterRegistry(bool writeBuiltin)
{
    // TODO: write to builtin USD types. For now we're creating these nodes as
    // Arnold-Typed primitives at the end of this function

    if (writeBuiltin) {
        RegisterWriter("polymesh", new UsdArnoldWriteMesh());
        RegisterWriter("curves", new UsdArnoldWriteCurves());
        RegisterWriter("points", new UsdArnoldWritePoints());

        // Register light writers
        RegisterWriter("distant_light", new UsdArnoldWriteDistantLight());
        RegisterWriter("skydome_light", new UsdArnoldWriteDomeLight());
        RegisterWriter("disk_light", new UsdArnoldWriteDiskLight());
        RegisterWriter("point_light", new UsdArnoldWriteSphereLight());
        RegisterWriter("quad_light", new UsdArnoldWriteRectLight());
        RegisterWriter("mesh_light", new UsdArnoldWriteGeometryLight());

        RegisterWriter("persp_camera", new UsdArnoldWriteCamera(UsdArnoldWriteCamera::CAMERA_PERSPECTIVE));
        RegisterWriter("ortho_camera", new UsdArnoldWriteCamera(UsdArnoldWriteCamera::CAMERA_ORTHOGRAPHIC));
    }

    // Now let's iterate over all the arnold classes known at this point
    bool universeCreated = false;
    // If a universe is already active, we can just use it, otherwise we need to
    // call AiBegin.
    //  But if we do so, we'll have to call AiEnd() when we finish
#if ARNOLD_VERSION_NUMBER >= 70100
    if (!AiArnoldIsActive()) {
#else
    if (!AiUniverseIsActive()) {
#endif
        AiBegin();
        universeCreated = true;
    }

    // Register a writer for ginstance, whose behaviour is a
    // bit special regarding default values
    RegisterWriter("ginstance", new UsdArnoldWriteGinstance());

    // Iterate over all node types
    AtNodeEntryIterator *nodeEntryIter = AiUniverseGetNodeEntryIterator(AI_NODE_ALL);
    while (!AiNodeEntryIteratorFinished(nodeEntryIter)) {
        AtNodeEntry *nodeEntry = AiNodeEntryIteratorGetNext(nodeEntryIter);
        std::string entryName = AiNodeEntryGetName(nodeEntry);

        // if a primWriter is already registed for this AtNodeEntry (i.e. from
        // the above list), then we should skip it. We want these nodes to be
        // exported as USD native primitive
        if (GetPrimWriter(entryName)) {
            continue;
        }

        std::string entryTypeName = AiNodeEntryGetTypeName(nodeEntry);

        std::string usdName = ArnoldUsdMakeCamelCase(entryName);
        if (usdName.length() == 0) {
            continue;
        }
        usdName[0] = toupper(usdName[0]);
        if (entryTypeName == "shader") {
            // We want to export all shaders as a UsdShader primitive,
            // and set the shader type in info:id
            usdName = std::string("arnold:") + entryName;
            RegisterWriter(entryName, new UsdArnoldWriteShader(entryName, usdName));
        } else if (
            AiNodeEntryGetType(nodeEntry) == AI_NODE_SHAPE &&
            AiNodeEntryGetDerivedType(nodeEntry) == AI_NODE_SHAPE_PROCEDURAL && entryName != "procedural" &&
            entryName != "alembic" && entryName != "usd") {
            // For custom procedurals, we want a dedicated schema "ArnoldProceduralCustom"
            RegisterWriter(entryName, new UsdArnoldWriteProceduralCustom(entryName));
        } else {
            // Generic writer for arnold nodes.
            usdName = std::string("Arnold") + usdName;
            RegisterWriter(entryName, new UsdArnoldWriteArnoldType(entryName, usdName, entryTypeName));
        }
    }
    AiNodeEntryIteratorDestroy(nodeEntryIter);

    if (universeCreated) {
        AiEnd();
    }
}
UsdArnoldWriterRegistry::~UsdArnoldWriterRegistry()
{
    // Delete all the prim readers that were registed here
    std::unordered_map<std::string, UsdArnoldPrimWriter *>::iterator it = _writersMap.begin();
    std::unordered_map<std::string, UsdArnoldPrimWriter *>::iterator itEnd = _writersMap.end();

    for (; it != itEnd; ++it) {
        delete it->second;
    }
}

/**
 *   Register a prim writer for a given Arnold node type, overriding the
 *eventual existing one.
 *
 **/
void UsdArnoldWriterRegistry::RegisterWriter(const std::string &primName, UsdArnoldPrimWriter *primWriter)
{
    std::unordered_map<std::string, UsdArnoldPrimWriter *>::iterator it = _writersMap.find(primName);
    if (it != _writersMap.end()) {
        // we have already registered a reader for this node type, let's delete
        // the existing one and override it
        delete it->second;
    }
    _writersMap[primName] = primWriter;
}
