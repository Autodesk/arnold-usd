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

#include <ai_msg.h>
#include <ai_nodes.h>
#include <pxr/usd/usd/prim.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "reader.h"
#include "utils.h"
#include <shape_utils.h>

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *   Base Class for a UsdPrim read. This class is in charge of converting a USD
 *primitive to Arnold
 *
 **/
class UsdArnoldPrimReader {
public:
    UsdArnoldPrimReader(int type = AI_NODE_ALL) : _type(type) {}
    virtual ~UsdArnoldPrimReader() {}

    virtual void Read(const UsdPrim &prim, UsdArnoldReaderContext &context) = 0;

    static void ReadAttribute(
        InputAttribute &attr, AtNode *node, const std::string &arnoldAttr, const TimeSettings &time,
        UsdArnoldReaderContext &context, int paramType, int arrayType = AI_TYPE_NONE);
    static void ReadPrimvars(
        const UsdPrim &prim, AtNode *node, const TimeSettings &time, UsdArnoldReaderContext &context,
        PrimvarsRemapper *primvarsRemapper = nullptr);

    int GetType() const { return _type; }

protected:
    static void _ReadArnoldParameters(
        const UsdPrim &prim, UsdArnoldReaderContext &context, AtNode *node, const TimeSettings &time,
        const std::string &scope = "arnold", bool acceptEmptyScope = false);
    static void _ReadArrayLink(
        const UsdPrim &prim, const UsdAttribute &attr, const TimeSettings &time, 
        UsdArnoldReaderContext &context, AtNode *node, const std::string &scope);
    static void _ReadAttributeConnection(
            const UsdAttribute &usdAttr, AtNode *node, const std::string &arnoldAttr,  
            const TimeSettings &time, UsdArnoldReaderContext &context, int paramType);

    int _type;
};

class UsdArnoldReadUnsupported : public UsdArnoldPrimReader {
public:
    UsdArnoldReadUnsupported(const std::string &typeName) : UsdArnoldPrimReader(), _typeName(typeName) {}
    void Read(const UsdPrim &prim, UsdArnoldReaderContext &context) override;

private:
    std::string _typeName;
};

#define REGISTER_PRIM_READER(name, t)                                             \
    class name : public UsdArnoldPrimReader {                                     \
    public:                                                                       \
        name() : UsdArnoldPrimReader(t) {}                                        \
        void Read(const UsdPrim &prim, UsdArnoldReaderContext &context) override; \
    };
