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

#include "prim_writer.h"

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *   The Shader Writer will be used to save an Arnold shader as a UsdShadeShader
 *primitive. This is a generic "Shader" primitive in USD, that stores the name
 *(id) of the shader in its attribute "info:id". Here all the shader names will
 *be prefixed by "Arnold" in order to recognize them, and they will be
 *camel-cased (standard_surface -> ArnoldStandardSurface). The input attributes
 *are expected to be in the "input" scope (e.g. input:base_color, etc...)
 **/
class UsdArnoldWriteShader : public UsdArnoldPrimWriter {
public:
    UsdArnoldWriteShader(const std::string &entryName, const std::string &usdShaderId)
        : UsdArnoldPrimWriter(), _entryName(entryName), _usdShaderId(usdShaderId)
    {
    }

    void Write(const AtNode *node, UsdArnoldWriter &writer) override;

private:
    std::string _entryName;   // node entry name for this node
    std::string _usdShaderId; // name (id) of this shader in USD-land
};
