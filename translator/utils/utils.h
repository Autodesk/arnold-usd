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
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/shader.h>

#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

// convert from "snake_case" to "camelCase"
// ignores the capitalization of input strings: letters are only capitalized
// if they follow an underscore
//
inline std::string makeCamelCase(const std::string &in)
{
    std::string out;
    out.reserve(in.length());
    bool capitalize = false;
    unsigned char c;
    for (size_t i = 0; i < in.length(); ++i) {
        c = in[i];
        if (c == '_') {
            capitalize = true;
        } else {
            if (capitalize) {
                c = toupper(c);
                capitalize = false;
            }
            out += c;
        }
    }
    return out;
}
