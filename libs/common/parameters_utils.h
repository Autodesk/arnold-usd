//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "api_adapter.h"
#include <pxr/usd/usd/prim.h>
#include <ai.h>
#include <string>
#include "timesettings.h"

#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/vt/array.h>

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

bool HasAuthoredAttribute(const UsdPrim &prim, const TfToken &attrName);bool HasAuthoredAttribute(const UsdPrim &prim, const TfToken &attrName);


static inline bool VtValueGetBool(const VtValue& value, bool defaultValue = false)
{
    if (value.IsHolding<bool>())
        return value.UncheckedGet<bool>();
    if (value.IsHolding<int>())
        return value.UncheckedGet<int>() != 0;
    if (value.IsHolding<long>())
        return value.UncheckedGet<long>() != 0;
    if (value.IsHolding<VtArray<bool>>()) {
        VtArray<bool> array = value.UncheckedGet<VtArray<bool>>();
        return array.empty() ? false : array[0];
    }
    if (value.IsHolding<VtArray<int>>()) {
        VtArray<int> array = value.UncheckedGet<VtArray<int>>();
        return array.empty() ? false : (array[0] != 0);
    }
    if (value.IsHolding<VtArray<long>>()) {
        VtArray<long> array = value.UncheckedGet<VtArray<long>>();
        return array.empty() ? false : (array[0] != 0);   
    }
    return defaultValue;
}

static inline float VtValueGetFloat(const VtValue& value, float defaultValue = 0.f)
{
    if (value.IsHolding<float>())
        return value.UncheckedGet<float>();

    if (value.IsHolding<double>())
        return static_cast<float>(value.UncheckedGet<double>());

    if (value.IsHolding<GfHalf>())
        return static_cast<float>(value.UncheckedGet<GfHalf>());

    if (value.IsHolding<VtArray<float>>()) {
        VtArray<float> array = value.UncheckedGet<VtArray<float>>();
        return array.empty() ? 0.f : array[0];
    }
    if (value.IsHolding<VtArray<double>>()) {
        VtArray<double> array = value.UncheckedGet<VtArray<double>>();
        return array.empty() ? 0.f : static_cast<float>(array[0]);
    }
    if (value.IsHolding<VtArray<GfHalf>>()) {
        VtArray<GfHalf> array = value.UncheckedGet<VtArray<GfHalf>>();
        return array.empty() ? 0.f : static_cast<float>(array[0]);
    }
    return defaultValue;
}

static inline unsigned char VtValueGetByte(const VtValue& value, unsigned char defaultValue = 0)
{
    if (value.IsHolding<int>())
        return static_cast<unsigned char>(value.UncheckedGet<int>());
    if (value.IsHolding<long>())
        return static_cast<unsigned char>(value.UncheckedGet<long>());
    if (value.IsHolding<unsigned char>())
        return value.UncheckedGet<unsigned char>();
    if (value.IsHolding<VtArray<unsigned char>>()) {
        VtArray<unsigned char> array = value.UncheckedGet<VtArray<unsigned char>>();
        return array.empty() ? 0 : array[0];
    }
    if (value.IsHolding<VtArray<int>>()) {
        VtArray<int> array = value.UncheckedGet<VtArray<int>>();
        return array.empty() ? 0 : array[0];   
    }
    if (value.IsHolding<VtArray<long>>()) {
        VtArray<long> array = value.UncheckedGet<VtArray<long>>();
        return array.empty() ? 0 : array[0];   
    }

    return defaultValue;
}

static inline int VtValueGetInt(const VtValue& value, int defaultValue = 0)
{
    if (value.IsHolding<int>())
        return value.UncheckedGet<int>();
    if (value.IsHolding<long>())
        return static_cast<int>(value.UncheckedGet<long>());
    if (value.IsHolding<VtArray<int>>()) {
        VtArray<int> array = value.UncheckedGet<VtArray<int>>();
        return array.empty() ? 0 : array[0];      
    }
    if (value.IsHolding<VtArray<long>>()) {
        VtArray<long> array = value.UncheckedGet<VtArray<long>>();
        return array.empty() ? 0 : (int) array[0];
    }

    return defaultValue;
}

static inline unsigned int VtValueGetUInt(const VtValue& value, unsigned int defaultValue = 0)
{
    if (value.IsHolding<unsigned int>()) {
        return value.UncheckedGet<unsigned int>();
    }
    if (value.IsHolding<int>()) {
        return static_cast<unsigned int>(value.UncheckedGet<int>());
    }
    if (value.IsHolding<unsigned char>()) {
        return static_cast<unsigned int>(value.UncheckedGet<unsigned char>());
    }
    if (value.IsHolding<VtArray<unsigned int>>()) {
        VtArray<unsigned int> array = value.UncheckedGet<VtArray<unsigned int>>();
        return array.empty() ? 0 : array[0];   
    }

    return defaultValue;
}

static inline GfVec2f VtValueGetVec2f(const VtValue& value, GfVec2f defaultValue = GfVec2f(0.f))
{
    if (value.IsHolding<GfVec2f>())
        return value.UncheckedGet<GfVec2f>();
    
    if (value.IsHolding<GfVec2d>()) {
        GfVec2d vecd = value.UncheckedGet<GfVec2d>();
        return GfVec2f(static_cast<float>(vecd[0]), static_cast<float>(vecd[1]));
    }
    if (value.IsHolding<GfVec2h>()) {
        GfVec2h vech = value.UncheckedGet<GfVec2h>();
        return GfVec2f(static_cast<float>(vech[0]), static_cast<float>(vech[1]));
    }
    
    if (value.IsHolding<VtArray<GfVec2f>>()) {
        VtArray<GfVec2f> array = value.UncheckedGet<VtArray<GfVec2f>>();
        return array.empty() ? GfVec2f(0.f, 0.f) : array[0];
    }
    if (value.IsHolding<VtArray<GfVec2d>>()) {
        VtArray<GfVec2d> array = value.UncheckedGet<VtArray<GfVec2d>>();
        return array.empty() ? GfVec2f(0.f, 0.f) : 
            GfVec2f(static_cast<float>(array[0][0]), static_cast<float>(array[0][1]));
    }
    if (value.IsHolding<VtArray<GfVec2h>>()) {
        VtArray<GfVec2h> array = value.UncheckedGet<VtArray<GfVec2h>>();
        return array.empty() ? GfVec2f(0.f, 0.f) : 
            GfVec2f(static_cast<float>(array[0][0]), static_cast<float>(array[0][1]));
    }    
    return defaultValue;
}

static inline GfVec3f VtValueGetVec3f(const VtValue& value, const GfVec3f defaultValue = GfVec3f(0.f))
{
    if (value.IsHolding<GfVec3f>())
        return value.UncheckedGet<GfVec3f>();
    
    if (value.IsHolding<GfVec3d>()) {
        GfVec3d vecd = value.UncheckedGet<GfVec3d>();
        return GfVec3f(static_cast<float>(vecd[0]), 
            static_cast<float>(vecd[1]), static_cast<float>(vecd[2]));
    }
    if (value.IsHolding<GfVec3h>()) {
        GfVec3h vech = value.UncheckedGet<GfVec3h>();
        return GfVec3f(static_cast<float>(vech[0]),
            static_cast<float>(vech[1]), static_cast<float>(vech[2]));
    }
    
    if (value.IsHolding<VtArray<GfVec3f>>()) {
        VtArray<GfVec3f> array = value.UncheckedGet<VtArray<GfVec3f>>();
        return array.empty() ? GfVec3f(0.f, 0.f, 0.f) : array[0];
    }
    if (value.IsHolding<VtArray<GfVec3d>>()) {
        VtArray<GfVec3d> array = value.UncheckedGet<VtArray<GfVec3d>>();
        return array.empty() ? GfVec3f(0.f, 0.f, 0.f) : 
            GfVec3f(static_cast<float>(array[0][0]), 
                static_cast<float>(array[0][1]), static_cast<float>(array[0][2]));
    }
    if (value.IsHolding<VtArray<GfVec3h>>()) {
        VtArray<GfVec3h> array = value.UncheckedGet<VtArray<GfVec3h>>();
        return array.empty() ? GfVec3f(0.f, 0.f, 0.f) : 
            GfVec3f(static_cast<float>(array[0][0]), 
                static_cast<float>(array[0][1]), static_cast<float>(array[0][2]));
    }
    if (value.IsHolding<GfVec4f>()) {
        GfVec4f v = value.UncheckedGet<GfVec4f>();
        return GfVec3f(v[0], v[1], v[2]);
    }
    if (value.IsHolding<GfVec4d>()) {
        GfVec4d v = value.UncheckedGet<GfVec4d>();
        return GfVec3f((float)v[0], (float)v[1], (float)v[2]);
    }
    return defaultValue;
}

static inline GfVec4f VtValueGetVec4f(const VtValue& value, const GfVec4f defaultValue = GfVec4f(0.f))
{
    if (value.IsHolding<GfVec4f>())
        return value.UncheckedGet<GfVec4f>();
    
    if (value.IsHolding<GfVec4d>()) {
        GfVec4d vecd = value.UncheckedGet<GfVec4d>();
        return GfVec4f(static_cast<float>(vecd[0]), static_cast<float>(vecd[1]), 
            static_cast<float>(vecd[2]), static_cast<float>(vecd[3]));
    }
    if (value.IsHolding<GfVec4h>()) {
        GfVec4h vech = value.UncheckedGet<GfVec4h>();
        return GfVec4f(static_cast<float>(vech[0]), static_cast<float>(vech[1]), 
            static_cast<float>(vech[2]), static_cast<float>(vech[3]));
    }
    
    if (value.IsHolding<VtArray<GfVec4f>>()) {
        VtArray<GfVec4f> array = value.UncheckedGet<VtArray<GfVec4f>>();
        return array.empty() ? GfVec4f(0.f, 0.f, 0.f, 0.f) : array[0];
    }
    if (value.IsHolding<VtArray<GfVec4d>>()) {
        VtArray<GfVec4d> array = value.UncheckedGet<VtArray<GfVec4d>>();
        return array.empty() ? GfVec4f(0.f, 0.f, 0.f, 0.f) : 
            GfVec4f(static_cast<float>(array[0][0]), static_cast<float>(array[0][1]), 
                static_cast<float>(array[0][2]), static_cast<float>(array[0][3]));
    }
    if (value.IsHolding<VtArray<GfVec4h>>()) {
        VtArray<GfVec4h> array = value.UncheckedGet<VtArray<GfVec4h>>();
        return array.empty() ? GfVec4f(0.f, 0.f, 0.f, 0.f) : 
            GfVec4f(static_cast<float>(array[0][0]), static_cast<float>(array[0][1]), 
                static_cast<float>(array[0][2]), static_cast<float>(array[0][3]));
    }    
    if (value.IsHolding<GfVec3f>()) {
        GfVec3f v = value.UncheckedGet<GfVec3f>();
        return GfVec4f(v[0], v[1], v[2], 1.f);
    }
    if (value.IsHolding<GfVec3d>()) {
        GfVec3d v = value.UncheckedGet<GfVec3d>();
        return GfVec4f((float)v[0], (float)v[1], (float)v[2], 1.f);
    }

    return defaultValue;
}

static inline std::string _VtValueResolvePath(const SdfAssetPath& assetPath, const UsdAttribute* attr = nullptr)
{
    std::string path = assetPath.GetResolvedPath();
    if (path.empty()) {
        path = assetPath.GetAssetPath();
        // If the filename has tokens ("<UDIM>") and is relative, USD won't resolve it and we end up here.
        // In this case we need to resolve the path to pass to arnold ourselves, by looking at the composition arcs in
        // this primitive. Note that we only need this for UsdUvTexture attribute "inputs:file"
        if (attr != nullptr && attr->GetName().GetString() == "inputs:file" && !path.empty() && TfIsRelativePath(path)) {
            UsdPrim prim = attr->GetPrim();
            if (prim && prim.IsA<UsdShadeShader>()) {
                UsdShadeShader shader(prim);
                TfToken id;
                shader.GetIdAttr().Get(&id);
                std::string shaderId = id.GetString();
                if (shaderId == "UsdUVTexture") {
                    // SdfComputeAssetPathRelativeToLayer returns search paths (vs anchored paths) unmodified,
                    // this is apparently to make sure they will be always searched again.
                    // This is not what we want, so we make sure the path is anchored
                    if (TfIsRelativePath(path) && path[0] != '.') {
                        path = "./" + path;
                    }
                    for (const auto& sdfProp : attr->GetPropertyStack()) {
                        const auto& layer = sdfProp->GetLayer();
                        if (layer && !layer->GetRealPath().empty()) {
                            std::string layerPath = SdfComputeAssetPathRelativeToLayer(layer, path);
                            if (!layerPath.empty() && layerPath != path &&
                                TfPathExists(layerPath.substr(0, layerPath.find_last_of("\\/")))) {
                                return layerPath;
                            }
                        }
                    }
                }
            }
        }
    }
    return path;
}

static inline std::string VtValueGetString(const VtValue& value, const UsdAttribute *attr = nullptr)
{
    if (value.IsHolding<std::string>()) {
        return value.UncheckedGet<std::string>();
    }
    if (value.IsHolding<TfToken>()) {
        TfToken token = value.UncheckedGet<TfToken>();
        return token.GetText();
    }
    if (value.IsHolding<SdfAssetPath>()) {
        SdfAssetPath assetPath = value.UncheckedGet<SdfAssetPath>();
        return _VtValueResolvePath(assetPath, attr);
    }
    if (value.IsHolding<VtArray<std::string>>()) {
        VtArray<std::string> array = value.UncheckedGet<VtArray<std::string>>();
        return array.empty() ? "" : array[0];
    }
    if (value.IsHolding<VtArray<TfToken>>()) {
        VtArray<TfToken> array = value.UncheckedGet<VtArray<TfToken>>();
        if (array.empty())
            return "";
        return array[0].GetText();
    }
    if (value.IsHolding<VtArray<SdfAssetPath>>()) {
        VtArray<SdfAssetPath> array = value.UncheckedGet<VtArray<SdfAssetPath>>();
        if (array.empty())
            return "";
        SdfAssetPath assetPath = array[0];
        return _VtValueResolvePath(assetPath, attr);
    }

    return std::string();
}

static inline bool VtValueGetMatrix(const VtValue& value, AtMatrix& matrix)
{
    if (value.IsHolding<GfMatrix4d>()) {
        GfMatrix4d usdMat = value.UncheckedGet<GfMatrix4d>();
        const double* array = usdMat.GetArray();
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j, array++) {
                matrix[i][j] = (float)*array;
            }
        }
    } else if (value.IsHolding<VtArray<GfMatrix4d>>()) {
        VtArray<GfMatrix4d> mtxArray = value.UncheckedGet<VtArray<GfMatrix4d>>();
        if (mtxArray.empty())
            return false;

        const GfMatrix4d &usdMat = mtxArray[0];
        const double* array = usdMat.GetArray();
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j, array++) {
                matrix[i][j] = (float)*array;
            }
        }
    } else if (value.IsHolding<GfMatrix4f>()) {
        GfMatrix4f usdMat = value.UncheckedGet<GfMatrix4f>();
        const float* array = usdMat.GetArray();
        memcpy(&matrix.data[0][0], array, 16 * sizeof(float));
    } else if (value.IsHolding<VtArray<GfMatrix4f>>()) {
        VtArray<GfMatrix4f> mtxArray = value.UncheckedGet<VtArray<GfMatrix4f>>();
        if (mtxArray.empty())
            return false;
        GfMatrix4f usdMat = mtxArray[0];
        const float* array = usdMat.GetArray();
        memcpy(&matrix.data[0][0], array, 16 * sizeof(float));
    } else {
        return false;
    }

    return true;
}
