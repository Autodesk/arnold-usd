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

#include <pxr/base/gf/matrix4f.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/shader.h>
#include <numeric>
#include <string>
#include <vector>

#include "../utils/utils.h"
#include <shape_utils.h>

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldReader;
class UsdArnoldReaderContext;

struct TimeSettings {
    TimeSettings() : frame(1.f), motionBlur(false), motionStart(1.f), motionEnd(1.f) {}

    float frame;
    bool motionBlur;
    float motionStart;
    float motionEnd;

    float start() const { return (motionBlur) ? motionStart + frame : frame; }
    float end() const { return (motionBlur) ? motionEnd + frame : frame; }
};

class PrimvarsRemapper
{
public:
    PrimvarsRemapper() {}
    virtual ~PrimvarsRemapper() {}  

    virtual bool RemapValues(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
        VtValue &value);
    virtual bool RemapIndexes(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
        std::vector<unsigned int> &indexes);
    virtual void RemapPrimvar(TfToken &name, std::string &interpolation);
};

struct InputAttribute {
    InputAttribute(const UsdAttribute& attribute) : attr(attribute), primvar(nullptr) {}
    InputAttribute(const UsdGeomPrimvar& primv) : attr(primv.GetAttr()), primvar(&primv) {}

    const UsdAttribute& GetAttr() { return attr; }

    bool Get(VtValue* value, float frame) const
    {
        bool res = false;
        if (primvar) {
            if (computeFlattened)
                res = primvar->ComputeFlattened(value, frame);
            else
                res = primvar->Get(value, frame);
        } else
            res = attr.Get(value, frame);

        if (primvar && primvarsRemapper)
            primvarsRemapper->RemapValues(*primvar, primvarInterpolation, *value);
        return res;

    }

    const UsdAttribute& attr;
    const UsdGeomPrimvar* primvar;
    bool computeFlattened = false;
    PrimvarsRemapper *primvarsRemapper = nullptr;
    TfToken primvarInterpolation;

};

/** Read Xformable transform as an arnold shape "matrix"
 */
void ReadMatrix(const UsdPrim& prim, AtNode* node, const TimeSettings& time, 
    UsdArnoldReaderContext& context, bool isXformable=true);

AtArray *ReadMatrix(const UsdPrim& prim, const TimeSettings& time, 
    UsdArnoldReaderContext& context, bool isXformable=true);


/** Read String arrays, and handle the conversion from std::string / TfToken to AtString.
 */
size_t ReadStringArray(UsdAttribute attr, AtNode* node, const char* attrName, const TimeSettings& time);

template <class U, class A>
size_t ReadArray(
    UsdAttribute attr, AtNode* node, const char* attrName, const TimeSettings& time, uint8_t attrType = AI_TYPE_NONE)
{
    InputAttribute inputAttr(attr);
    return ReadArray<U, A>(inputAttr, node, attrName, time, attrType);
}
/** Convert a USD array attribute (type U), to an Arnold array (type A).
 *  When both types are identical, we can simply their pointer to create the
 *array. Otherwise we need to copy the data first
 **/
template <class U, class A>
size_t ReadArray(
    InputAttribute& attr, AtNode* node, const char* attrName, const TimeSettings& time, uint8_t attrType = AI_TYPE_NONE)
{
    bool sameData = std::is_same<U, A>::value;
    const UsdAttribute& usdAttr = attr.GetAttr();

    if (attrType == AI_TYPE_NONE) {
        if (std::is_same<A, float>::value)
            attrType = AI_TYPE_FLOAT;
        else if (std::is_same<A, int>::value)
            attrType = AI_TYPE_INT;
        else if (std::is_same<A, bool>::value)
            attrType = AI_TYPE_BOOLEAN;
        else if (std::is_same<A, unsigned int>::value)
            attrType = AI_TYPE_UINT;
        else if (std::is_same<A, unsigned char>::value)
            attrType = AI_TYPE_BYTE;
        else if (std::is_same<A, GfVec3f>::value)
            attrType = AI_TYPE_VECTOR;
        else if (std::is_same<A, AtRGB>::value)
            attrType = AI_TYPE_RGB;
        else if (std::is_same<A, AtRGBA>::value)
            attrType = AI_TYPE_RGBA;
        else if (std::is_same<A, GfVec4f>::value)
            attrType = AI_TYPE_RGBA;
        else if (std::is_same<A, TfToken>::value)
            attrType = AI_TYPE_STRING;
        else if (std::is_same<A, std::string>::value)
            attrType = AI_TYPE_STRING;
        else if (std::is_same<A, GfMatrix4f>::value)
            attrType = AI_TYPE_MATRIX;
        else if (std::is_same<A, GfMatrix4d>::value)
            attrType = AI_TYPE_MATRIX;
        else if (std::is_same<A, AtMatrix>::value) {
            if (std::is_same<U, GfMatrix4f>::value)
                sameData = true;
            attrType = AI_TYPE_MATRIX;
        } else if (std::is_same<A, AtVector>::value) {
            attrType = AI_TYPE_VECTOR;
            if (std::is_same<U, GfVec3f>::value) // AtVector is represented the same
                                                 // way as GfVec3f
                sameData = true;
        } else if (std::is_same<A, GfVec2f>::value)
            attrType = AI_TYPE_VECTOR2;
        else if (std::is_same<A, AtVector2>::value) {
            attrType = AI_TYPE_VECTOR2;
            if (std::is_same<U, GfVec2f>::value) // AtVector2 is represented the
                                                 // same way as GfVec2f
                sameData = true;
        }
    }

    // Call a dedicated function for string conversions
    if (attrType == AI_TYPE_STRING)
        return ReadStringArray(usdAttr, node, attrName, time);

    bool animated = time.motionBlur && usdAttr.ValueMightBeTimeVarying();

    if (!animated) {
        // Single-key arrays
        VtValue val;
        if (!attr.Get(&val, time.frame))
            return 0;

        const VtArray<U>* array = &(val.Get<VtArray<U>>());

        size_t size = array->size();
        if (size > 0) {
            if (std::is_same<U, GfMatrix4d>::value) {
                // special case for matrices. They're single
                // precision in arnold but double precision in USD,
                // and there is no copy from one to the other.
                VtArray<GfMatrix4d>* arrayMtx = (VtArray<GfMatrix4d>*)(array);
                GfMatrix4d* matrices = arrayMtx->data();
                std::vector<AtMatrix> arnoldVec(size);
                for (size_t v = 0; v < size; ++v) {
                    AtMatrix& aiMat = arnoldVec[v];
                    const double* matArray = matrices[v].GetArray();
                    for (unsigned int i = 0; i < 4; ++i)
                        for (unsigned int j = 0; j < 4; ++j)
                            aiMat[i][j] = matArray[4 * i + j];
                }
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, 1, AI_TYPE_MATRIX, &arnoldVec[0]));

            } else if (sameData) {
                // The USD data representation is the same as the Arnold one, we don't
                // need to convert the data
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, 1, attrType, array->cdata()));
            } else {
                // Different data representation between USD and Arnold, we need to
                // copy the vector. Note that we could instead allocate the AtArray
                // and set the elements one by one, but I'm assuming it's faster
                // this way
                VtArray<A> arnold_vec;
                arnold_vec.assign(array->cbegin(), array->cend());
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, 1, attrType, arnold_vec.cdata()));
            }
        } else
            AiNodeResetParameter(node, AtString(attrName));

        return 1; // return the amount of keys
    } else {
        // Animated array
        GfInterval interval(time.start(), time.end(), false, false);
        std::vector<double> timeSamples;
        usdAttr.GetTimeSamplesInInterval(interval, &timeSamples);
        // need to add the start end end keys (interval has open bounds)
        size_t numKeys = timeSamples.size() + 2;

        float timeStep = float(interval.GetMax() - interval.GetMin()) / int(numKeys - 1);
        float timeVal = interval.GetMin();

        VtValue val;
        if (!attr.Get(&val, timeVal))
            return 0;

        const VtArray<U>* array = &(val.Get<VtArray<U>>());

        // Arnold arrays don't support varying element counts per key.
        // So if we find that the size changes over time, we will just take a single key for the current frame        
        size_t size = array->size();
        if (size == 0) {
            AiNodeResetParameter(node, AtString(attrName));
            return 0;
        }
        if (std::is_same<U, GfMatrix4d>::value) {
            VtArray<AtMatrix> arnoldVec(size * numKeys);
            int index = 0;

            for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
                if (i > 0) {
                    if (!attr.Get(&val, timeVal)) {
                        continue;
                    }
                    array = &(val.Get<VtArray<U>>());
                }
                VtArray<GfMatrix4d>* arrayMtx = (VtArray<GfMatrix4d>*)(array);
                GfMatrix4d* matrices = arrayMtx->data();
                if (arrayMtx->size() != size) {
                    // Arnold won't support varying element count. 
                    // We need to only consider a single key corresponding to the current frame
                    arnoldVec.clear();
                    if (!attr.Get(&val, time.frame))
                        break;

                    index = 0;
                    array = &(val.Get<VtArray<U>>());
                    size = array->size(); // update size to the current frame one
                    numKeys = 1; // we just want a single key
                    arnoldVec.resize(size);
                    arrayMtx = (VtArray<GfMatrix4d>*)(array);
                    matrices = arrayMtx->data();
                    i = numKeys; // this will stop the "for" loop
                }

                for (size_t v = 0; v < size; ++v, ++index) {
                    AtMatrix& aiMat = arnoldVec[index];
                    const double* matArray = matrices[v].GetArray();
                    for (unsigned int k = 0; k < 4; ++k)
                        for (unsigned int j = 0; j < 4; ++j)
                            aiMat[k][j] = matArray[4 * k + j];
                }
            }
            AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, numKeys, AI_TYPE_MATRIX, arnoldVec.data()));
        } else {
            std::vector<A> arnoldVec;
            arnoldVec.reserve(size * numKeys);
            for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
                if (i > 0) {
                    if (!attr.Get(&val, timeVal)) {
                        return 0;
                    }
                    array = &(val.Get<VtArray<U>>());
                }
                if (array->size() != size) {
                     // Arnold won't support varying element count. 
                    // We need to only consider a single key corresponding to the current frame
                    arnoldVec.clear();
                    if (!attr.Get(&val, time.frame))
                        break;

                    array = &(val.Get<VtArray<U>>()); 
                    size = array->size(); // update size to the current frame one
                    numKeys = 1; // we just want a single key now
                    arnoldVec.reserve(size);
                    i = numKeys; // this will stop the "for" loop
                }
                arnoldVec.insert(arnoldVec.end(), array->begin(), array->end());
            }
            AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, numKeys, attrType, &arnoldVec[0]));
        }
        return numKeys;
    }
}

/**
 *  Read all primvars from this shape, and set them as arnold user data
 *
 **/

// Read the materials / shaders assigned to a shape (node)
void ReadMaterialBinding(const UsdPrim& prim, AtNode* node, UsdArnoldReaderContext& context, bool assignDefault = true);

// Read the materials / shaders assigned to a shape (node)
void ReadSubsetsMaterialBinding(
    const UsdPrim& prim, AtNode* node, UsdArnoldReaderContext& context, std::vector<UsdGeomSubset>& subsets,
    unsigned int elementCount, bool assignDefault = true);

/**
 * Read a specific shader parameter from USD to Arnold
 *
 **/
void ReadShaderParameter(
    UsdShadeShader& shader, AtNode* node, const std::string& usdName, const std::string& arnoldName,
    UsdArnoldReaderContext& context);

static inline bool VtValueGetBool(const VtValue& value)
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
    return value.Get<bool>();
}

static inline float VtValueGetFloat(const VtValue& value)
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
    return value.Get<float>();
}

static inline unsigned char VtValueGetByte(const VtValue& value)
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

    return value.Get<unsigned char>();
}

static inline int VtValueGetInt(const VtValue& value)
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

    return value.Get<int>();
}

static inline unsigned int VtValueGetUInt(const VtValue& value)
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

    return value.Get<unsigned int>();
}

static inline GfVec2f VtValueGetVec2f(const VtValue& value)
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
    return value.Get<GfVec2f>();
}

static inline GfVec3f VtValueGetVec3f(const VtValue& value)
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
    return value.Get<GfVec3f>();
}

static inline GfVec4f VtValueGetVec4f(const VtValue& value)
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
    return value.Get<GfVec4f>();
}

static inline std::string VtValueGetString(const VtValue& value)
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
        std::string path = assetPath.GetResolvedPath();
        if (path.empty()) {
            path = assetPath.GetAssetPath();
        }
        return path;
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
        std::string path = assetPath.GetResolvedPath();
        if (path.empty()) {
            path = assetPath.GetAssetPath();
        }
        return path;
    }

    return value.Get<std::string>();
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

bool IsPrimVisible(const UsdPrim &prim, UsdArnoldReader *reader, float frame);
