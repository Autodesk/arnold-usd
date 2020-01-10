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
#include "write_arnold_type.h"

#include <ai.h>

#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *    Write out any Arnold node to a generic "typed"  USD primitive (eg
 *ArnoldSetParameter, ArnoldDriverExr, etc...). In this write function, we need
 *to create the USD primitive, then loop over the Arnold node attributes, and
 *write them to the USD file. Note that we could (should) use the schemas to do
 *this, but since the conversion is simple, for now we're hardcoding it here.
 *For now the attributes are prefixed with "arnold:" as this is what is done in
 *the schemas. But this is something that we could remove in the future, as it's
 *not strictly needed.
 **/
void UsdArnoldWriteArnoldType::write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = getArnoldNodeName(node); // what should be the name of this USD primitive
    UsdStageRefPtr stage = writer.getUsdStage();    // get the current stage defined in the writer
    SdfPath objPath(nodeName);

    UsdPrim prim = stage->GetPrimAtPath(objPath);
    if (prim && prim.IsActive()) {
        // This primitive was already written, let's early out
        return;
    }
    prim = stage->DefinePrim(objPath, TfToken(_usdName));

    writeArnoldParameters(node, writer, prim, "");
}
