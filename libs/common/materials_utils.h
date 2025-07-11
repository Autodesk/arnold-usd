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

/// MaterialReader class is used by both usd and hydra translators, with
/// the information needed to translate a shading tree
class MaterialReader
{
public:
    MaterialReader() {}

    virtual AtNode* CreateArnoldNode(const char* nodeType, const char* nodeName) = 0;
    virtual void ConnectShader(AtNode* node, const std::string& attrName, 
            const SdfPath& target, ArnoldAPIAdapter::ConnectionType type) = 0;
    virtual bool GetShaderInput(const SdfPath& shaderPath, const TfToken& param,
                        VtValue& value, TfToken& shaderId) = 0;

};

/// Convert a shader from Hydra / USD to Arnold
///
/// @param nodeName  Name of the Arnold node to create
/// @param shaderId  Type of the input shader to read
/// @param inputAtts Map of input attributes, containing VtValues and connections. They're stored with attribute names as keys
/// @param context   ArnoldAPIAdapter used in this conversion
/// @param time      TimeSettings class with the required information for motion blur
/// @param materialReader  Reference to a MaterialReader class, used for this translation
/// @return Pointer to the created Arnold node for this input shader. When multiple arnold shaders are created, the "root" shader is returned.
AtNode* ReadShader(const std::string& nodeName, const TfToken& shaderId, 
    const InputAttributesList& inputAttrs, ArnoldAPIAdapter& context, 
    const TimeSettings& time, MaterialReader& materialReader);

inline std::string GetArnoldShaderName(const SdfPath& nodePath, const SdfPath& materialPath)
{
    return nodePath.HasPrefix(materialPath) ? nodePath.GetString() : materialPath.GetString() + nodePath.GetString();
}
