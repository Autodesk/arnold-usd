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
#include <ai_node_entry.h>
#include <ai_nodes.h>
#include <ai_params.h>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "writer.h"

PXR_NAMESPACE_USING_DIRECTIVE

/**
 *   Base Class for a UsdPrim writer. This class is in charge of converting
 *Arnold primitives to USD
 *
 **/
class UsdArnoldPrimWriter {
public:
    UsdArnoldPrimWriter() {}
    virtual ~UsdArnoldPrimWriter() {}
    
    void writeNode(const AtNode *node, UsdArnoldWriter &writer);
    
    // Helper structure to convert parameters
    struct ParamConversion {
        const SdfValueTypeName &type;
        std::function<VtValue(const AtNode *, const char *)> f;
        std::function<bool(const AtNode *, const char *, const AtParamValue *pentry)> d;

        ParamConversion(
            const SdfValueTypeName &_type, std::function<VtValue(const AtNode *, const char *)> _f,
            std::function<bool(const AtNode *, const char *, const AtParamValue *pentry)> _d)
            : type(_type), f(std::move(_f)), d(std::move(_d))
        {
        }
    };

    // get the proper conversion for the given arnold param type
    static const ParamConversion *getParamConversion(uint8_t type);
    // This function returns the name we want to give to this AtNode when it's
    // converted to USD
    static std::string getArnoldNodeName(const AtNode *node);
    bool writeAttribute(const AtNode *node, const char *paramName, UsdPrim &prim, const UsdAttribute &attr, UsdArnoldWriter &writer);
    

protected:
    virtual void write(const AtNode *node, UsdArnoldWriter &writer) = 0;        
    void writeArnoldParameters(const AtNode *node, UsdArnoldWriter &writer, UsdPrim &prim, const std::string &scope="arnold");
    void writeMatrix(UsdGeomXformable &xform, const AtNode *node, UsdArnoldWriter &writer);
    void writeMaterialBinding(const AtNode *node, UsdPrim &prim, UsdArnoldWriter &writer);
    std::unordered_set<std::string> _exportedAttrs; // list of arnold attributes that were exported     
};

/**
 *  UsdArnoldWriteUnsupported is a prim writer for node types that aren't
 *supported (yet). When trying to convert one of these nodes, an error message
 *will appear
 **/
class UsdArnoldWriteUnsupported : public UsdArnoldPrimWriter {
public:
    UsdArnoldWriteUnsupported(const std::string &type) : UsdArnoldPrimWriter(), _type(type) {}
    void write(const AtNode *node, UsdArnoldWriter &writer) override;

private:
    std::string _type;
};

// Helper macro for prim writers
#define REGISTER_PRIM_WRITER(name)                                        \
    class name : public UsdArnoldPrimWriter {                             \
    protected:                                                               \
        void write(const AtNode *node, UsdArnoldWriter &writer) override; \
    };
