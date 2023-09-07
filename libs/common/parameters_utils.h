//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "api_adapter.h"
#include <pxr/usd/usd/prim.h>
#include <ai.h>
#include <string>
#include "timesettings.h"

// The following file should ultimatelly belongs to common
#include "../translator/reader/utils.h" // InputAttribute, PrimvarsRemapper


PXR_NAMESPACE_USING_DIRECTIVE

void ValidatePrimPath(std::string &path, const UsdPrim &prim);

void ReadAttribute(
        const UsdPrim &prim, InputAttribute &attr, AtNode *node, const std::string &arnoldAttr, const TimeSettings &time,
        ArnoldAPIAdapter &context, int paramType, int arrayType = AI_TYPE_NONE);

void ReadPrimvars(
        const UsdPrim &prim, AtNode *node, const TimeSettings &time, ArnoldAPIAdapter &context,
        PrimvarsRemapper *primvarsRemapper = nullptr);


void ReadArnoldParameters(
        const UsdPrim &prim, ArnoldAPIAdapter &context, AtNode *node, const TimeSettings &time,
        const std::string &scope = "arnold");

void _ReadArrayLink(
        const UsdPrim &prim, const UsdAttribute &attr, const TimeSettings &time, 
        ArnoldAPIAdapter &context, AtNode *node, const std::string &scope);

void _ReadAttributeConnection(
            const UsdPrim &prim, const UsdAttribute &usdAttr, AtNode *node, const std::string &arnoldAttr,  
            const TimeSettings &time, ArnoldAPIAdapter &context, int paramType);
