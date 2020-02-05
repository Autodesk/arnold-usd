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
#include <string>
#include <vector>

#include <pxr/usd/usd/prim.h>

#include "prim_reader.h"

PXR_NAMESPACE_USING_DIRECTIVE

// Register readers for the USD builtin types
class UsdArnoldReadArnoldType : public UsdArnoldPrimReader {
public:
    UsdArnoldReadArnoldType(const std::string &entryName, const std::string &entryTypeName)
        : UsdArnoldPrimReader(), _entryName(entryName), _entryTypeName(entryTypeName)
    {
    }
    void read(const UsdPrim &prim, UsdArnoldReaderContext &context) override;

private:
    std::string _entryName;
    std::string _entryTypeName;
};
