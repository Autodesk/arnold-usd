//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Autodesk, Inc.
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
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdShade/shader.h>

#include <numeric>
#include <string>
#include <vector>

#include "../utils/utils.h"
#include <shape_utils.h>

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldReader;
class UsdArnoldReaderContext;

#include "timesettings.h"

// TODO: primvarsremapper and inputattribute classes should be moved out from this file
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

    bool Get(VtValue* value, double frame) const
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

AtArray *ReadLocalMatrix(const UsdPrim &prim, const TimeSettings &time);

/** Read String arrays, and handle the conversion from std::string / TfToken to AtString.
 */
inline
size_t ReadStringArray(UsdAttribute attr, AtNode *node, const char *attrName, const TimeSettings &time)
{
    // Strings can be represented in USD as std::string, TfToken or SdfAssetPath.
    // We'll try to get the input attribute value as each of these types
    VtArray<std::string> arrayStr;
    VtArray<TfToken> arrayToken;
    VtArray<SdfAssetPath> arrayPath;
    AtArray *outArray = nullptr;
    size_t size;

    if (attr.Get(&arrayStr, time.frame)) {
        size = arrayStr.size();
        if (size > 0) {
            outArray = AiArrayAllocate(size, 1, AI_TYPE_STRING);
            for (size_t i = 0; i < size; ++i) {
                if (!arrayStr[i].empty())
                    AiArraySetStr(outArray, i, AtString(arrayStr[i].c_str()));
                else
                    AiArraySetStr(outArray, i, AtString(""));
            }
        }
    } else if (attr.Get(&arrayToken, time.frame)) {
        size = arrayToken.size();
        if (size > 0) {
            outArray = AiArrayAllocate(size, 1, AI_TYPE_STRING);
            for (size_t i = 0; i < size; ++i) {
                if (!arrayToken[i].GetString().empty())
                    AiArraySetStr(outArray, i, AtString(arrayToken[i].GetText()));
                else
                    AiArraySetStr(outArray, i, AtString(""));
            }
        }
    } else if (attr.Get(&arrayPath, time.frame)) {
        size = arrayPath.size();
        if (size > 0) {
            outArray = AiArrayAllocate(size, 1, AI_TYPE_STRING);
            for (size_t i = 0; i < size; ++i) {
                if (!arrayPath[i].GetResolvedPath().empty())
                    AiArraySetStr(outArray, i, AtString(arrayPath[i].GetResolvedPath().c_str()));
                else
                    AiArraySetStr(outArray, i, AtString(""));
            }
        }
    }

    if (outArray)
        AiNodeSetArray(node, AtString(attrName), outArray);
    else
        AiNodeResetParameter(node, AtString(attrName));

    return 1; // return the amount of motion keys
}

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
        if (!attr.Get(&val, time.frame)) {
            // Create an empty array
            AiNodeSetArray(node, AtString(attrName), AiArrayConvert(0, 1, attrType, nullptr));
            return 0;
        }


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
                // copy the vector. 
                VtArray<A> arnold_vec;
                arnold_vec.reserve(array->size());
                for (const auto &elem : (*array))
                    arnold_vec.push_back(static_cast<A>(elem));
                
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

        double timeStep = double(interval.GetMax() - interval.GetMin()) / double(numKeys - 1);
        double timeVal = interval.GetMin();

        VtValue val;
        if (!attr.Get(&val, timeVal)) {
            // Create an empty array
            AiNodeSetArray(node, AtString(attrName), AiArrayConvert(0, 1, attrType, nullptr));
            return 0;
        }

        const VtArray<U>* array = &(val.Get<VtArray<U>>());

        // Arnold arrays don't support varying element counts per key.
        // So if we find that the size changes over time, we will just take a single key for the current frame        
        size_t size = array->size();
        if (size == 0)
            return 0;
        
        if (std::is_same<U, GfMatrix4d>::value) {
            VtArray<AtMatrix> arnoldVec(size * numKeys);
            int index = 0;

            for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
                if (i > 0) {
                    // if a time sample is missing, we can't translate 
                    // this attribute properly
                    if (!attr.Get(&val, timeVal))
                        return 0;
                    
                    array = &(val.Get<VtArray<U>>());
                }
                VtArray<GfMatrix4d>* arrayMtx = (VtArray<GfMatrix4d>*)(array);
                GfMatrix4d* matrices = arrayMtx->data();
                if (arrayMtx->size() != size) {
                    // Arnold won't support varying element count. 
                    // We need to only consider a single key corresponding to the current frame
                    arnoldVec.clear();
                    if (!attr.Get(&val, time.frame)) 
                        return 0;
                
                    index = 0;
                    array = &(val.Get<VtArray<U>>());
                    size = array->size(); // update size to the current frame one
                    if (size == 0)
                        return 0;
                
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
            if (size > 0)
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, numKeys, AI_TYPE_MATRIX, arnoldVec.data()));
        } else {
            A* arnoldVec = new A[size * numKeys], *ptr = arnoldVec;
            for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
                if (i > 0) {
                    // if a time sample is missing, we can't translate 
                    // this attribute properly
                    if (!attr.Get(&val, timeVal)) {
                        size = 0;
                        break;
                    }
                    array = &(val.Get<VtArray<U>>());
                }
                if (array->size() != size) {
                     // Arnold won't support varying element count. 
                    // We need to only consider a single key corresponding to the current frame
                    if (!attr.Get(&val, time.frame)) {
                        size = 0;
                        break;
                    }                        

                    delete [] arnoldVec;
                    array = &(val.Get<VtArray<U>>()); 
                    size = array->size(); // update size to the current frame one
                    numKeys = 1; // we just want a single key now
                    // reallocate the array
                    arnoldVec = new A[size * numKeys];
                    ptr = arnoldVec;
                    i = numKeys; // this will stop the "for" loop after the concatenation
                    
                }
                for (unsigned j=0; j < array->size(); j++)
                    *ptr++ = array->data()[j];
            }

            if (size > 0)
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, numKeys, attrType, arnoldVec));
            else
                numKeys = 0;

            delete [] arnoldVec;
        }
        return numKeys;
    }
}

size_t ReadTopology(
    UsdAttribute& usdAttr, AtNode* node, const char* attrName, const TimeSettings& time, UsdArnoldReaderContext &context);
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

bool IsPrimVisible(const UsdPrim &prim, UsdArnoldReader *reader, float frame);

void ApplyParentMatrices(AtArray *matrices, const AtArray *parentMatrices);

void ReadLightShaders(const UsdPrim& prim, const UsdAttribute &attr, AtNode *node, UsdArnoldReaderContext &context);
void ReadCameraShaders(const UsdPrim& prim, AtNode *node, UsdArnoldReaderContext &context);

// The normals can be set on primvars:normals or just normals. 
// primvars:normals takes "precedence" over "normals"
template <typename UsdGeomT>
inline UsdAttribute GetNormalsAttribute(const UsdGeomT &usdGeom) {
    UsdGeomPrimvarsAPI primvarsAPI(usdGeom.GetPrim());
    if (primvarsAPI) {
        UsdGeomPrimvar normalsPrimvar = primvarsAPI.GetPrimvar(TfToken("normals"));
        if (normalsPrimvar) {
            return normalsPrimvar.GetAttr();
        }
    }
    return usdGeom.GetNormalsAttr();
}

template <typename UsdGeomT>
inline TfToken GetNormalsInterpolation(const UsdGeomT &usdGeom) {
    UsdGeomPrimvarsAPI primvarsAPI(usdGeom.GetPrim());
    if (primvarsAPI) {
        UsdGeomPrimvar normalsPrimvar = primvarsAPI.GetPrimvar(TfToken("normals"));
        if (normalsPrimvar) {
            return normalsPrimvar.GetInterpolation();
        }
    }
    return usdGeom.GetNormalsInterpolation();
}

int GetTimeSampleNumKeys(const UsdPrim &geom, const TimeSettings &tim, TfToken interpolation=UsdGeomTokens->constant);
