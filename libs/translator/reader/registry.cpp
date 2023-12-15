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
#include "registry.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "read_arnold_type.h"
#include "read_camera.h"
#include "read_geometry.h"
#include "read_light.h"
#include "read_options.h"
#include "read_shader.h"
#include "utils.h"
#include <common_utils.h>
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

void UsdArnoldReaderRegistry::RegisterPrimitiveReaders()
{
    Clear(); // Start from scratch

    // First, let's register all the prim readers that we've hardcoded for USD
    // builtin types

    // USD builtin Shapes
    RegisterReader("Mesh", new UsdArnoldReadMesh());
    RegisterReader("Curves", new UsdArnoldReadCurves());
    RegisterReader("BasisCurves", new UsdArnoldReadCurves());
    RegisterReader("NurbsCurves", new UsdArnoldReadCurves());
    RegisterReader("Points", new UsdArnoldReadPoints());
    RegisterReader("Cube", new UsdArnoldReadCube());
    RegisterReader("Sphere", new UsdArnoldReadSphere());
    RegisterReader("Cylinder", new UsdArnoldReadCylinder());
    RegisterReader("Cone", new UsdArnoldReadCone());
    RegisterReader("Capsule", new UsdArnoldReadCapsule());
    RegisterReader("PointInstancer", new UsdArnoldReadPointInstancer());
    RegisterReader("Nurbs", new UsdArnoldReadUnsupported("Nurbs"));
    RegisterReader("Volume", new UsdArnoldReadVolume());

    RegisterReader("DistantLight", new UsdArnoldReadDistantLight());
    RegisterReader("DomeLight", new UsdArnoldReadDomeLight());
    RegisterReader("DiskLight", new UsdArnoldReadDiskLight());
    RegisterReader("SphereLight", new UsdArnoldReadSphereLight());
    RegisterReader("RectLight", new UsdArnoldReadRectLight());
    RegisterReader("GeometryLight", new UsdArnoldReadGeometryLight());
    RegisterReader("CylinderLight", new UsdArnoldReadCylinderLight());
    RegisterReader("Camera", new UsdArnoldReadCamera());

    // USD Shaders (builtin, or custom ones, including arnold)
    UsdArnoldPrimReader *shaderReader = new UsdArnoldReadShader();
    RegisterReader("Shader", shaderReader);

    RegisterReader("NodeGraph", new UsdArnoldReadNodeGraph(*shaderReader));
    RegisterReader("Material", new UsdArnoldReadNodeGraph(*shaderReader));


    // Register reader for USD Render Settings schemas. Note that the
    // eventual RenderProduct, RenderVar primitives referenced by the
    // RenderSettings will be translated by this reader (and not independantly)
    RegisterReader("RenderSettings", new UsdArnoldReadRenderSettings());

    // Now let's iterate over all the arnold classes known at this point
    bool universeCreated = false;
    // If a universe is already active, we can just use it, otherwise we need to
    // call AiBegin.
    //  But if we do so, we'll have to call AiEnd() when we finish
#if ARNOLD_VERSION_NUM >= 70100
    if (!AiArnoldIsActive()) {
#else
    if (!AiUniverseIsActive()) {
#endif
        AiBegin();
        universeCreated = true;
    }

    // Iterate over all node types
    AtNodeEntryIterator *nodeEntryIter = AiUniverseGetNodeEntryIterator(AI_NODE_ALL);
    while (!AiNodeEntryIteratorFinished(nodeEntryIter)) {
        AtNodeEntry *nodeEntry = AiNodeEntryIteratorGetNext(nodeEntryIter);
        std::string entryName = AiNodeEntryGetName(nodeEntry);

        // Do we need different behaviour depending on the entry type name ?
        std::string entryTypeName = AiNodeEntryGetTypeName(nodeEntry);
        int nodeEntryType = AiNodeEntryGetType(nodeEntry);

        // FIXME: should we switch to camel case for usd node types ?
        std::string usdName = ArnoldUsdMakeCamelCase(entryName);
        if (usdName.length() == 0) {
            continue;
        }
        usdName[0] = toupper(usdName[0]);
        usdName = std::string("Arnold") + usdName;
        RegisterReader(usdName, new UsdArnoldReadArnoldType(entryName, entryTypeName, nodeEntryType));
    }
    AiNodeEntryIteratorDestroy(nodeEntryIter);

    // Generic schema for custom procedurals
    RegisterReader("ArnoldProceduralCustom", new UsdArnoldReadProceduralCustom());

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
        RegisterReader("Mesh", new UsdArnoldReadBounds(_params));
        RegisterReader("Curves", new UsdArnoldReadBounds(_params));
        RegisterReader("Points", new UsdArnoldReadBounds(_params));
        RegisterReader("Cube", new UsdArnoldReadBounds(_params));
        RegisterReader("Sphere", new UsdArnoldReadBounds(_params));
        RegisterReader("Cylinder", new UsdArnoldReadBounds(_params));
        RegisterReader("Cone", new UsdArnoldReadBounds(_params));
        RegisterReader("Capsule", new UsdArnoldReadBounds(_params));
    } else if (_mode == AI_PROC_POLYGONS) {
        RegisterReader("Mesh", new UsdArnoldReadGenericPolygons(_params));
    } else if (_mode == AI_PROC_POINTS) {
        RegisterReader("Mesh", new UsdArnoldReadGenericPoints(_params));
        RegisterReader("Curves", new UsdArnoldReadGenericPoints(_params));
        RegisterReader("Points", new UsdArnoldReadGenericPoints(_params));
    }

    static AtString proceduralsOnlyStr("procedurals_only");
    bool proceduralsOnly = false;
    if (_params && AiParamValueMapGetBool(_params, proceduralsOnlyStr, &proceduralsOnly) && proceduralsOnly) {
        // in procedurals only mode, we want to return the procedurals node itself instead of expanding it
        RegisterReader("ArnoldProcedural", new UsdArnoldReadArnoldType("procedural", "shape", AI_NODE_SHAPE)); 
        RegisterReader("ArnoldUsd",  new UsdArnoldReadArnoldType("usd", "shape", AI_NODE_SHAPE)); 
        RegisterReader("ArnoldAlembic",  new UsdArnoldReadArnoldType("alembic", "shape", AI_NODE_SHAPE)); 
        RegisterReader("ArnoldProceduralCustom",  new UsdArnoldReadProceduralCustom());

    } else {
        // For procedurals that can be read a scene format (ass, abc, usd),
        // we use a prim reader that will load the scene in this universe
        RegisterReader("ArnoldProcedural", new UsdArnoldReadProcViewport("procedural", _mode, _params));
        RegisterReader("ArnoldUsd", new UsdArnoldReadProcViewport("usd", _mode, _params));
        RegisterReader("ArnoldAlembic", new UsdArnoldReadProcViewport("alembic", _mode, _params));
        // For custom procedurals, use the same reader but with an empty procName
        RegisterReader("ArnoldProceduralCustom", new UsdArnoldReadProcViewport("", _mode, _params));
    }
}
#endif
