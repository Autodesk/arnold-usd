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
#include "registry.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "read_arnold_type.h"
#include "read_geometry.h"
#include "read_light.h"
#include "read_shader.h"
#include "utils.h"
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

void UsdArnoldReaderRegistry::registerPrimitiveReaders()
{
    // First, let's register all the prim readers that we've hardcoded for USD
    // builtin types

    // USD builtin Shapes
    registerReader("Mesh", new UsdArnoldReadMesh());
    registerReader("Curves", new UsdArnoldReadCurves());
    registerReader("Points", new UsdArnoldReadPoints());
    registerReader("Cube", new UsdArnoldReadCube());
    registerReader("Sphere", new UsdArnoldReadSphere());
    registerReader("Cylinder", new UsdArnoldReadCylinder());
    registerReader("Cone", new UsdArnoldReadCone());
    registerReader("Capsule", new UsdArnoldReadCapsule());
    registerReader("PointInstancer", new UsdArnoldPointInstancer());
    registerReader("Nurbs", new UsdArnoldReadUnsupported("Nurbs"));

    // USD builtin Lights
    registerReader("DistantLight", new UsdArnoldReadDistantLight());
    registerReader("DomeLight", new UsdArnoldReadDomeLight());
    registerReader("DiskLight", new UsdArnoldReadDiskLight());
    registerReader("SphereLight", new UsdArnoldReadSphereLight());
    registerReader("RectLight", new UsdArnoldReadRectLight());
    registerReader("GeometryLight", new UsdArnoldReadGeometryLight());

    // USD Shaders (builtin, or custom ones, including arnold)
    registerReader("Shader", new UsdArnoldReadShader());

    // Now let's iterate over all the arnold classes known at this point
    bool universeCreated = false;
    // If a universe is already active, we can just use it, otherwise we need to
    // call AiBegin.
    //  But if we do so, we'll have to call AiEnd() when we finish
    if (!AiUniverseIsActive()) {
        AiBegin();
        universeCreated = true;
        // FIXME: should we call AiLoadPlugins here, or will it be done
        // automatically ?
    }

    // Iterate over all node types
    AtNodeEntryIterator *nodeEntryIter = AiUniverseGetNodeEntryIterator(AI_NODE_ALL);
    while (!AiNodeEntryIteratorFinished(nodeEntryIter)) {
        AtNodeEntry *nodeEntry = AiNodeEntryIteratorGetNext(nodeEntryIter);
        std::string entryName = AiNodeEntryGetName(nodeEntry);

        // Do we need different behaviour depending on the entry type name ?
        std::string entryTypeName = AiNodeEntryGetTypeName(nodeEntry);

        // FIXME: should we switch to camel case for usd node types ?
        std::string usdName = makeCamelCase(entryName);
        if (usdName.length() == 0) {
            continue;
        }
        usdName[0] = toupper(usdName[0]);
        usdName = std::string("Arnold") + usdName;
        registerReader(usdName, new UsdArnoldReadArnoldType(entryName, entryTypeName));
    }
    AiNodeEntryIteratorDestroy(nodeEntryIter);

    if (universeCreated) {
        AiEnd();
    }
}
UsdArnoldReaderRegistry::~UsdArnoldReaderRegistry()
{
    // Delete all the prim readers that were registed here
    std::unordered_map<std::string, UsdArnoldPrimReader *>::iterator it = _readersMap.begin();
    std::unordered_map<std::string, UsdArnoldPrimReader *>::iterator itEnd = _readersMap.end();

    for (; it != itEnd; ++it) {
        delete it->second;
    }
}

void UsdArnoldReaderRegistry::registerReader(const std::string &primName, UsdArnoldPrimReader *primReader)
{
    std::unordered_map<std::string, UsdArnoldPrimReader *>::iterator it = _readersMap.find(primName);
    if (it != _readersMap.end()) {
        // we have already registered a reader for this node type, let's delete
        // the existing one and override it
        delete it->second;
    }
    _readersMap[primName] = primReader;
}


// The viewport API is introduced in Arnold 6.0.0. I
// It defines AtProcViewportMode and AtParamValueMap, which are needed by this class
#if AI_VERSION_ARCH_NUM >= 6
void UsdArnoldViewportReaderRegistry::registerPrimitiveReaders()
{
    // Do *not* call the parent function, we don't want to register the default nodes here

    // TODO: support Arnold schemas like ArnoldPolymesh, etc...
    if (_mode == AI_PROC_BOXES)
    {
        registerReader("Mesh", new UsdArnoldReadBounds());
        registerReader("Curves", new UsdArnoldReadBounds());
        registerReader("Points", new UsdArnoldReadBounds());
        registerReader("Cube", new UsdArnoldReadBounds());
        registerReader("Sphere", new UsdArnoldReadBounds());
        registerReader("Cylinder", new UsdArnoldReadBounds());
        registerReader("Cone", new UsdArnoldReadBounds());
        registerReader("Capsule", new UsdArnoldReadBounds());
    } else if (_mode == AI_PROC_POLYGONS)
    {
        registerReader("Mesh", new UsdArnoldReadGenericPolygons());
    } else if (_mode == AI_PROC_POINTS)
    {
        registerReader("Mesh", new UsdArnoldReadGenericPoints());
        registerReader("Curves", new UsdArnoldReadGenericPoints());
        registerReader("Points", new UsdArnoldReadGenericPoints());
    }
}
#endif
