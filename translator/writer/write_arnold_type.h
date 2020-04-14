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
#include <vector>

#include "prim_writer.h"

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *    Prim Writers for generic Arnold nodes. These nodes will be saved as
 *"typed" schemas, with a node type prefixed with "Arnold", and camel-cased
 *names. For example, set_parameter will be saved as a typed usd node
 *"ArnoldSetParameter". For now the attribute are saved with the "arnold:"
 *namespace, but this could be changed as the namespace isn't strictly needed on
 *typed schemas
 **/
class UsdArnoldWriteArnoldType : public UsdArnoldPrimWriter {
public:
    UsdArnoldWriteArnoldType(const std::string &entryName, const std::string &usdName, const std::string &entryTypeName)
        : UsdArnoldPrimWriter(), _entryName(entryName), _usdName(usdName), _entryTypeName(entryTypeName)
    {
    }
    virtual ~UsdArnoldWriteArnoldType() {}
protected:
    void write(const AtNode *node, UsdArnoldWriter &writer) override;

    std::string _entryName;
    std::string _usdName;
    std::string _entryTypeName;
};

/**  
 *   Ginstance nodes require a special treatment, because of the behaviour of
 *   default values. In general we can skip authoring an attribute if the value
 *   is different from default, but that's not the case for instances. Here, 
 *   we'll compare the value of the attribute with the corresponding value
 *   for the instanced node. If it is different we will write it, even if the 
 *   value is equal to default 
 **/
class UsdArnoldWriteGinstance : public UsdArnoldWriteArnoldType {
public:
    UsdArnoldWriteGinstance()
        : UsdArnoldWriteArnoldType("ginstance", "ArnoldGinstance", "shape") {}

    virtual ~UsdArnoldWriteGinstance() {}

protected:
    void write(const AtNode *node, UsdArnoldWriter &writer) override;

    void processInstanceAttribute(UsdPrim &prim, const AtNode *node, const AtNode *target, 
        						  const char *attrName, int attrType);
};
