//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "api_adapter.h"
#include <pxr/usd/usd/prim.h>
#include <ai.h>
#include <string>
#include "timesettings.h"
#include "api_adapter.h"
#include "parameters_utils.h"

#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usd/attribute.h>

PXR_NAMESPACE_USING_DIRECTIVE

class MaterialReader
{
public:
    MaterialReader() {}

    virtual AtNode* CreateArnoldNode(const char* nodeType, const char* nodeName) = 0;
    virtual void ConnectShader(AtNode* node, const std::string& attrName, 
            const SdfPath& target) = 0;
    virtual bool GetShaderInput(const SdfPath& shaderPath, const TfToken& param,
                        VtValue& value, TfToken& shaderId) = 0;

};

AtNode* ReadShader(const std::string& nodeName, const TfToken& shaderId, 
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter& context, 
    const TimeSettings& time, MaterialReader& materialReader);
