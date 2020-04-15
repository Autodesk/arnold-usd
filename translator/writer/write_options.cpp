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
#include "write_options.h"

#include <ai.h>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <iostream>

#include "registry.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *  Export Arnold options node. This will be exported in a very similar way than
 *  other UsdArnoldWriteType nodes, except that it needs a special treatment for
 *  the outputs attribute
 **/

void UsdArnoldWriteOptions::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    UsdStageRefPtr stage = writer.GetUsdStage();
    UsdPrim prim = stage->DefinePrim(SdfPath("/options"), TfToken("ArnoldOptions"));

    // We need a special treatment for the outputs array
    AtArray *outputs = AiNodeGetArray(node, "outputs");
    unsigned int numOutputs = (outputs) ? AiArrayGetNumElements(outputs) : 0;
    if (numOutputs > 0) {
        UsdAttribute outputsAttr = prim.CreateAttribute(TfToken("outputs"), SdfValueTypeNames->StringArray, false);
        VtArray<std::string> vtArray(numOutputs);
        for (unsigned int i = 0; i < numOutputs; ++i) {
            AtString output = AiArrayGetStr(outputs, i);
            std::string outputStr(output.c_str());
            // split the array with empty space, replace driver names
            std::string outStr;
            std::istringstream f(outputStr);
            std::string s;
            AtNode *outputsNode = nullptr;
            int ind = 0;
            while (std::getline(f, s, ' ')) {
                if (ind > 1 && (outputsNode = AiNodeLookUpByName(s.c_str()))) {
                    // convert the name
                    s = GetArnoldNodeName(outputsNode);
                }
                if (ind > 0)
                    outStr += " ";
                outStr += s;
                ind++;
            }
            vtArray[i] = outStr.c_str();
        }
        outputsAttr.Set(vtArray);
    }

    _exportedAttrs.insert("outputs");
    _WriteArnoldParameters(node, writer, prim, "");
}
