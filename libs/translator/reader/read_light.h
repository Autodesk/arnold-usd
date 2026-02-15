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
#pragma once

#include <ai_nodes.h>

#include <pxr/usd/usd/prim.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "prim_reader.h"

PXR_NAMESPACE_USING_DIRECTIVE

// Register readers for the USD builtin lights
REGISTER_PRIM_READER(UsdArnoldReadDistantLight, AI_NODE_LIGHT);
REGISTER_PRIM_READER(UsdArnoldReadDomeLight, AI_NODE_LIGHT);
REGISTER_PRIM_READER(UsdArnoldReadDiskLight, AI_NODE_LIGHT);
REGISTER_PRIM_READER(UsdArnoldReadSphereLight, AI_NODE_LIGHT);
REGISTER_PRIM_READER(UsdArnoldReadRectLight, AI_NODE_LIGHT);
REGISTER_PRIM_READER(UsdArnoldReadGeometryLight, AI_NODE_LIGHT);
REGISTER_PRIM_READER(UsdArnoldReadCylinderLight, AI_NODE_LIGHT);

void ReadLightCommon(const UsdPrim& prim, AtNode *node, const TimeSettings &time);
void ReadLightNormalize(const UsdPrim& prim, AtNode *node, const TimeSettings &time);
void ReadLightShapingParams(const UsdPrim& prim, AtNode* node, const TimeSettings& time, bool checkShaping = true);