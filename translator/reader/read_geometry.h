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
#pragma once

#include <ai_nodes.h>

#include <pxr/usd/usd/prim.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "prim_reader.h"

PXR_NAMESPACE_USING_DIRECTIVE

// Register readers for the USD builtin Geometries

REGISTER_PRIM_READER(UsdArnoldReadMesh);
REGISTER_PRIM_READER(UsdArnoldReadCurves);
REGISTER_PRIM_READER(UsdArnoldReadPoints);
REGISTER_PRIM_READER(UsdArnoldReadCube);
REGISTER_PRIM_READER(UsdArnoldReadSphere);
REGISTER_PRIM_READER(UsdArnoldReadCylinder);
REGISTER_PRIM_READER(UsdArnoldReadCone);
REGISTER_PRIM_READER(UsdArnoldReadCapsule);
REGISTER_PRIM_READER(UsdArnoldReadBounds);
REGISTER_PRIM_READER(UsdArnoldReadGenericPolygons);
REGISTER_PRIM_READER(UsdArnoldReadGenericPoints);
REGISTER_PRIM_READER(UsdArnoldPointInstancer);

/*

// USD builtin Shaders
REGISTER_PRIM_READER(UsdArnoldReadShader);
*/
