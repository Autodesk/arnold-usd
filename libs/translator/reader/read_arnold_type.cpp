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
#include "read_arnold_type.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include "utils.h"
#include "constant_strings.h"
#include <parameters_utils.h>

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/** Read Arnold-native nodes
 *
 **/
void UsdArnoldReadArnoldType::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode(_entryName.c_str(), prim.GetPath().GetText());

    const TimeSettings &time = context.GetTimeSettings();
    std::string objType = prim.GetTypeName().GetText();
    int nodeEntryType = AiNodeEntryGetType(AiNodeGetNodeEntry(node));

    // For arnold nodes that have a transform matrix, we read it as in a 
    //UsdGeomXformable : FIXME we could test if IsA<UsdGeomXformable>
    if (nodeEntryType == AI_NODE_SHAPE 
        || nodeEntryType == AI_NODE_CAMERA || nodeEntryType == AI_NODE_LIGHT)
    {
        ReadMatrix(prim, node, time, context, false); //false = not a xformable

        // If this arnold node is a shape, let's read the materials
        if (nodeEntryType == AI_NODE_SHAPE)
            ReadMaterialBinding(prim, node, context, false);
    }

    // The only job here is to look for arnold specific attributes and
    // convert them. If this primitive if a UsdShader "Shader" type, we're
    // looking for an attribute namespace "inputs", otherwise this is just an
    // arnold typed schema and we don't want any namespace.
    if (objType == "Shader")
    	ReadArnoldParameters(prim, context, node, time, "inputs");
    else {
    	// the last argument is set to true in order to be backwards compatible
    	// and to keep supporting usd files authored with previous versions of USD
    	// (before #583). To be removed
    	ReadArnoldParameters(prim, context, node, time, "arnold"); 
    }
    ReadPrimvars(prim, node, time, context);

    if (nodeEntryType == AI_NODE_SHAPE) {
        // For shape nodes, we want to check the prim visibility, 
        // and eventually set the AtNode visibility to 0 if it's hidden
        if (!context.GetPrimVisibility(prim, time.frame))
            AiNodeSetByte(node, str::visibility, 0);
    }
}
