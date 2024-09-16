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
#include <common_utils.h>
#include "prim_reader.h"

PXR_NAMESPACE_USING_DIRECTIVE

// Register readers for shaders relying on UsdShade
class UsdArnoldReadShader : public UsdArnoldPrimReader {

public:
    UsdArnoldReadShader() : UsdArnoldPrimReader(AI_NODE_SHADER | AI_NODE_IMAGER) {}
    AtNode* Read(const UsdPrim &prim, UsdArnoldReaderContext &context) override;
    static void ReadShaderInputs(const UsdPrim& prim, UsdArnoldReaderContext& context, 
    	AtNode* node);
private:
	static void _ReadShaderParameter(UsdShadeShader &shader, AtNode *node, 
		const std::string &usdAttr, const std::string &arnoldAttr,
		UsdArnoldReaderContext &context);
	static void _ReadShaderInput(const UsdShadeInput& input, AtNode* node, 
    	const std::string& arnoldAttr, UsdArnoldReaderContext& context);


};

// Register readers for shaders relying on UsdShade
class UsdArnoldReadNodeGraph : public UsdArnoldPrimReader {

public:
    UsdArnoldReadNodeGraph(UsdArnoldPrimReader& shaderReader) : 
    	_shaderReader(shaderReader),
    	UsdArnoldPrimReader(AI_NODE_SHADER) {}

    AtNode* Read(const UsdPrim &prim, UsdArnoldReaderContext &context) override;
   
private:
	UsdArnoldPrimReader& _shaderReader;
};
