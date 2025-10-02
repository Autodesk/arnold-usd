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

#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/vt/array.h>


PXR_NAMESPACE_USING_DIRECTIVE

struct InputAttribute {
    InputAttribute() {}
    ~InputAttribute() {delete timeValues;}

    VtValue value;
    std::vector<VtValue> *timeValues = nullptr;        
    SdfPath connection;
};

using InputAttributesList = std::unordered_map<TfToken, InputAttribute, TfToken::HashFunctor>;

void ReadAttribute(const InputAttribute &attr, AtNode *node, const std::string &arnoldAttr, const TimeSettings &time,
    ArnoldAPIAdapter &context, int paramType, int arrayType = AI_TYPE_NONE);


struct ValueReader {
    ValueReader() {}
    virtual bool Get(VtValue *value, double time) = 0;
};

void CreateInputAttribute(InputAttribute& inputAttr, const UsdAttribute& attr, const TimeSettings& time,
            int paramType, int arrayType = AI_TYPE_NONE, ValueReader* reader = nullptr);

void ReadAttribute(const UsdAttribute &attr, AtNode *node, const std::string &arnoldAttr, const TimeSettings &time,
                ArnoldAPIAdapter &context, int paramType, int arrayType = AI_TYPE_NONE);

void ReadArnoldParameters(
        const UsdPrim &prim, ArnoldAPIAdapter &context, AtNode *node, const TimeSettings &time,
        const std::string &scope = "arnold");

void ReadArrayLink(
    const UsdPrim &prim, const UsdAttribute &attr, const TimeSettings &time, 
    ArnoldAPIAdapter &context, AtNode *node, const std::string &scope);

bool HasAuthoredAttribute(const UsdPrim &prim, const TfToken &attrName);

bool VtValueGetBool(const VtValue& value, bool defaultValue = false);
float VtValueGetFloat(const VtValue& value, float defaultValue = 0.f);
unsigned char VtValueGetByte(const VtValue& value, unsigned char defaultValue = 0);
int VtValueGetInt(const VtValue& value, int defaultValue = 0);
unsigned int VtValueGetUInt(const VtValue& value, unsigned int defaultValue = 0);
GfVec2f VtValueGetVec2f(const VtValue& value, GfVec2f defaultValue = GfVec2f(0.f));
GfVec3f VtValueGetVec3f(const VtValue& value, GfVec3f defaultValue = GfVec3f(0.f));
GfVec4f VtValueGetVec4f(const VtValue& value, GfVec4f defaultValue = GfVec4f(0.f));
std::string VtValueGetString(const VtValue& value);
AtMatrix VtValueGetMatrix(const VtValue& value);
AtArray *VtValueGetArray(const std::vector<VtValue>& values, uint8_t arnoldType, ArnoldAPIAdapter &context);

std::string VtValueResolvePath(const SdfAssetPath& assetPath);

// Template function to cast different types of values, between Arnold and USD
template <typename CastTo, typename CastFrom>
inline void ConvertValue(CastTo& to, const CastFrom& from)
{
    // default to static cast
    to = static_cast<CastTo>(from);
}

// std::string to AtString
template <>
inline void ConvertValue<AtString, std::string>(AtString& to, const std::string& from)
{
    to = AtString{from.c_str()};
}

// TfToken to AtString
template <>
inline void ConvertValue<AtString, TfToken>(AtString& to, const TfToken& from)
{
    to = AtString{from.GetText()};
}

// SdfAssetPath to AtString
template <>
inline void ConvertValue<AtString, SdfAssetPath>(AtString& to, const SdfAssetPath& from)
{
    std::string resolvedPath = VtValueResolvePath(from);
    to = AtString{resolvedPath.c_str()};
}

// TfToken to std::string
template <>
inline void ConvertValue<std::string, TfToken>(std::string& to, const TfToken& from)
{
    to = std::string{from.GetText()};
}

// SdfAssetPath to std::string
template <>
inline void ConvertValue<std::string, SdfAssetPath>(std::string& to, const SdfAssetPath& from)
{    
    to = VtValueResolvePath(from);
}

// GfMatrix4f to AtMatrix
template <>
inline void ConvertValue<AtMatrix, GfMatrix4f>(AtMatrix& to, const GfMatrix4f& from)
{
    const float* array = from.GetArray();
    memcpy(&to.data[0][0], array, 16 * sizeof(float));
}
// GfMatrix4d to AtMatrix
template <>
inline void ConvertValue<AtMatrix, GfMatrix4d>(AtMatrix& to, const GfMatrix4d& from)
{
    // rely on GfMatrix conversions
    GfMatrix4f gfMatrix(from);
    const float* array = gfMatrix.GetArray();
    memcpy(&to.data[0][0], array, 16 * sizeof(float));
}
// AtMatrix to GfMatrix4f
template <>
inline void ConvertValue<GfMatrix4f, AtMatrix>(GfMatrix4f& to, const AtMatrix& from)
{
    memcpy(to.GetArray(), &from.data[0][0], 16 * sizeof(float));
}
// AtMatrix to GfMatrix4d
template <>
inline void ConvertValue<GfMatrix4d, AtMatrix>(GfMatrix4d& to, const AtMatrix& from)
{
    GfMatrix4f tmp;
    memcpy(tmp.GetArray(), &from.data[0][0], 16 * sizeof(float));
    to = GfMatrix4d(tmp);
}
bool DeclareArnoldAttribute(AtNode* node, const char* name, const char* scope, const char* type);

uint32_t DeclareAndAssignParameter(
    AtNode* node, const TfToken& name, const TfToken& scope, const VtValue& value, ArnoldAPIAdapter &context, bool isColor = false);

uint8_t GetArnoldTypeFromValue(const VtValue& value, bool includeArray = false);

