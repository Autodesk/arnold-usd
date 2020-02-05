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
#include "read_arnold_type.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/** Read Arnold-native nodes
 *
 **/
void UsdArnoldReadArnoldType::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{    
    AtNode *node = context.createArnoldNode(_entryName.c_str(), prim.GetPath().GetText());
    
    const TimeSettings &time = context.getTimeSettings();
    std::string objType = prim.GetTypeName().GetText();
    // The only job here is to look for arnold specific attributes and
    // convert them. If this primitive if a UsdShader "Shader" type, we're
    // looking for an attribute namespace "inputs", otherwise this is just an
    // arnold typed schema and we don't want any namespace.
    readArnoldParameters(prim, context, node, time, (objType == "Shader") ? "inputs" : "");    
}
