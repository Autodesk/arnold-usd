//
// SPDX-License-Identifier: Apache-2.0
//


#pragma once

#include <pxr/usd/usdGeom/primvar.h>

PXR_NAMESPACE_OPEN_SCOPE

// This is a base class used to call Arnold API functions within a particular context.
// For example we might want to wrap the AiNode call and add mutex, or store the nodes dependending on the context.
class ArnoldAPIAdapter {
public:
    // Type of connection between 2 nodes
    enum ConnectionType {
        CONNECTION_LINK = 0,
        CONNECTION_PTR = 1,
        CONNECTION_ARRAY
    };

    virtual AtNode *CreateArnoldNode(const char *type, const char *name) = 0;

    virtual void AddConnection(AtNode *source, const std::string &attr, const std::string &target, 
        ConnectionType type, const std::string &outputElement = std::string()) = 0;

    virtual void AddNodeName(const std::string &name, AtNode *node) = 0;

    // Ideally GetPrimvars shouldn't be here
    virtual const std::vector<UsdGeomPrimvar> &GetPrimvars() const = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE