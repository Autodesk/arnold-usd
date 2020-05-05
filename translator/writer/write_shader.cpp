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
#include "write_shader.h"

#include <ai.h>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "registry.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *  Export Arnold shaders as UsdShadeShader primitives. The output primitive
 *type is a generic "shader", and the actual shader name will be set in the
 *"info:id" attribute. Input parameter are saved in the "input:" namespace.
 **/

namespace {

inline void SplitString(const std::string &input, std::vector<std::string> &result)
{
    std::string::size_type start = 0;
    while (true) {
        // delimiters: semicolon and space
        std::string::size_type semicolon = input.find(";", start);
        std::string::size_type space = input.find(" ", start);
        std::string::size_type end = std::min(semicolon, space);
        bool notFound = (end == std::string::npos);
        std::string::size_type width = (notFound ? input.length() : end) - start;
        std::string name = input.substr(start, width);
        if (!name.empty())
            result.push_back(name);
        if (notFound)
            break;
        start = end + 1;
    }
}

} // namespace

void UsdArnoldWriteShader::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    UsdShadeShader shaderAPI = UsdShadeShader::Define(writer.GetUsdStage(), SdfPath(GetArnoldNodeName(node)));
    // set the info:id parameter to the actual shader name
    shaderAPI.CreateIdAttr().Set(TfToken(_usdShaderId));
    UsdPrim prim = shaderAPI.GetPrim();
    _WriteArnoldParameters(node, writer, prim, "inputs");
}
