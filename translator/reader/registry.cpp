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

void UsdArnoldReaderRegistry::RegisterPrimitiveReaders()
{
    Clear(); // Start from scratch

    // First, let's register all the prim readers that we've hardcoded for USD
    // builtin types

    // USD builtin Shapes
    if (_mask & AI_NODE_SHAPE) {
        RegisterReader("Mesh", new UsdArnoldReadMesh());
        RegisterReader("Curves", new UsdArnoldReadCurves());
        RegisterReader("BasisCurves", new UsdArnoldReadCurves());
        RegisterReader("Points", new UsdArnoldReadPoints());
        RegisterReader("Cube", new UsdArnoldReadCube());
        RegisterReader("Sphere", new UsdArnoldReadSphere());
        RegisterReader("Cylinder", new UsdArnoldReadCylinder());
        RegisterReader("Cone", new UsdArnoldReadCone());
        RegisterReader("Capsule", new UsdArnoldReadCapsule());
        RegisterReader("PointInstancer", new UsdArnoldReadPointInstancer());
        RegisterReader("Nurbs", new UsdArnoldReadUnsupported("Nurbs"));
        RegisterReader("Volume", new UsdArnoldReadVolume());
    }

    // USD builtin Lights
    if (_mask & AI_NODE_LIGHT) {
        RegisterReader("DistantLight", new UsdArnoldReadDistantLight());
        RegisterReader("DomeLight", new UsdArnoldReadDomeLight());
        RegisterReader("DiskLight", new UsdArnoldReadDiskLight());
        RegisterReader("SphereLight", new UsdArnoldReadSphereLight());
        RegisterReader("RectLight", new UsdArnoldReadRectLight());
        RegisterReader("GeometryLight", new UsdArnoldReadGeometryLight());
    }

    // USD Shaders (builtin, or custom ones, including arnold)
    if (_mask & AI_NODE_SHADER)
        RegisterReader("Shader", new UsdArnoldReadShader());

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
        int nodeEntryType = AiNodeEntryGetType(nodeEntry);
        if (!(nodeEntryType & _mask)) // This node type isn't meant to be read
            continue;

        // FIXME: should we switch to camel case for usd node types ?
        std::string usdName = MakeCamelCase(entryName);
        if (usdName.length() == 0) {
            continue;
        }
        usdName[0] = toupper(usdName[0]);
        usdName = std::string("Arnold") + usdName;
        RegisterReader(usdName, new UsdArnoldReadArnoldType(entryName, entryTypeName));
    }
    AiNodeEntryIteratorDestroy(nodeEntryIter);

    if (universeCreated) {
        AiEnd();
    }
}
UsdArnoldReaderRegistry::~UsdArnoldReaderRegistry()
{
    // Delete all the prim readers that were registed here
    Clear();
}

void UsdArnoldReaderRegistry::Clear()
{
    std::unordered_map<std::string, UsdArnoldPrimReader *>::iterator it = _readersMap.begin();
    std::unordered_map<std::string, UsdArnoldPrimReader *>::iterator itEnd = _readersMap.end();

    for (; it != itEnd; ++it) {
        delete it->second;
    }
    _readersMap.clear();
}
void UsdArnoldReaderRegistry::RegisterReader(const std::string &primName, UsdArnoldPrimReader *primReader)
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
void UsdArnoldViewportReaderRegistry::RegisterPrimitiveReaders()
{
    // Do *not* call the parent function, we don't want to register the default nodes here

    // TODO: support Arnold schemas like ArnoldPolymesh, etc...
    if (_mode == AI_PROC_BOXES) {
        RegisterReader("Mesh", new UsdArnoldReadBounds());
        RegisterReader("Curves", new UsdArnoldReadBounds());
        RegisterReader("Points", new UsdArnoldReadBounds());
        RegisterReader("Cube", new UsdArnoldReadBounds());
        RegisterReader("Sphere", new UsdArnoldReadBounds());
        RegisterReader("Cylinder", new UsdArnoldReadBounds());
        RegisterReader("Cone", new UsdArnoldReadBounds());
        RegisterReader("Capsule", new UsdArnoldReadBounds());
    } else if (_mode == AI_PROC_POLYGONS) {
        RegisterReader("Mesh", new UsdArnoldReadGenericPolygons());
    } else if (_mode == AI_PROC_POINTS) {
        RegisterReader("Mesh", new UsdArnoldReadGenericPoints());
        RegisterReader("Curves", new UsdArnoldReadGenericPoints());
        RegisterReader("Points", new UsdArnoldReadGenericPoints());
    }
}
#endif
