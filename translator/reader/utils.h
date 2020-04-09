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
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdShade/shader.h>

#include <numeric>
#include <string>
#include <vector>

#include "../utils/utils.h"

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldReader;
class UsdArnoldReaderContext;

struct MeshOrientation {
    MeshOrientation() : reverse(false) {}

    VtIntArray nsides_array;
    bool reverse;
    template <class T>
    void orientFaceIndexAttribute(T& attr);
};

struct TimeSettings {
    TimeSettings() : frame(1.f), motion_blur(false), motion_start(1.f), motion_end(1.f) {}

    float frame;
    bool motion_blur;
    float motion_start;
    float motion_end;

    float start() const { return (motion_blur) ? motion_start + frame : frame; }
    float end() const { return (motion_blur) ? motion_end + frame : frame; }
};

struct InputAttribute {
    InputAttribute(const UsdAttribute &attribute): attr(attribute), primvar(nullptr), computeFlattened(false) {}
    InputAttribute(const UsdGeomPrimvar &primv): attr(primv.GetAttr()), primvar(&primv), computeFlattened(false) {}

    const UsdAttribute &GetAttr() {return attr;}

    bool Get(VtValue *value, float frame) const
    {
        if (primvar) {
            if (computeFlattened)
                return primvar->ComputeFlattened(value, frame);
            else
                return primvar->Get(value, frame);
        } else 

        return attr.Get(value, frame);
    }

    const UsdAttribute &attr;
    const UsdGeomPrimvar *primvar;
    bool computeFlattened;
};
// Reverse an attribute of the face. Basically, it converts from the clockwise
// to the counterclockwise and back.
template <class T>
void MeshOrientation::orientFaceIndexAttribute(T& attr)
{
    if (!reverse)
        return;

    size_t counter = 0;
    for (auto npoints : nsides_array) {
        for (size_t j = 0; j < npoints / 2; j++) {
            size_t from = counter + j;
            size_t to = counter + npoints - 1 - j;
            std::swap(attr[from], attr[to]);
        }
        counter += npoints;
    }
}

/** Export Xformable transform as an arnold shape "matrix"
 */
void exportMatrix(const UsdPrim& prim, AtNode* node, const TimeSettings& time, UsdArnoldReaderContext &context);

/** Export String arrays, and handle the conversion from std::string / TfToken to AtString.
 */
size_t exportStringArray(UsdAttribute attr, AtNode* node, const char* attr_name, const TimeSettings& time);


template <class U, class A>
size_t exportArray(UsdAttribute attr, AtNode* node, const char* attr_name, const TimeSettings& time, uint8_t attr_type = AI_TYPE_NONE) 
{
    InputAttribute inputAttr(attr);
    return exportArray<U, A>(inputAttr, node, attr_name, time, attr_type);

}
/** Convert a USD array attribute (type U), to an Arnold array (type A).
 *  When both types are identical, we can simply their pointer to create the
 *array. Otherwise we need to copy the data first
 **/
template <class U, class A>
size_t exportArray(InputAttribute &attr, AtNode* node, const char* attr_name, const TimeSettings& time, uint8_t attr_type = AI_TYPE_NONE) 
{
    bool same_data = std::is_same<U, A>::value;
    const UsdAttribute &usdAttr = attr.GetAttr();

    if (attr_type == AI_TYPE_NONE) {
        if (std::is_same<A, float>::value)
            attr_type = AI_TYPE_FLOAT;
        else if (std::is_same<A, int>::value)
            attr_type = AI_TYPE_INT;
        else if (std::is_same<A, bool>::value)
            attr_type = AI_TYPE_BOOLEAN;
        else if (std::is_same<A, unsigned int>::value)
            attr_type = AI_TYPE_UINT;
        else if (std::is_same<A, unsigned char>::value)
            attr_type = AI_TYPE_BYTE;
        else if (std::is_same<A, GfVec3f>::value)
            attr_type = AI_TYPE_VECTOR;
        else if (std::is_same<A, AtRGB>::value)
            attr_type = AI_TYPE_RGB;
        else if (std::is_same<A, AtRGBA>::value)
            attr_type = AI_TYPE_RGBA;
        else if (std::is_same<A, GfVec4f>::value)
            attr_type = AI_TYPE_RGBA;
        else if (std::is_same<A, TfToken>::value)
            attr_type = AI_TYPE_STRING;
        else if (std::is_same<A, std::string>::value)
            attr_type = AI_TYPE_STRING;
        else if (std::is_same<A, GfMatrix4f>::value)
            attr_type = AI_TYPE_MATRIX;
        else if (std::is_same<A, GfMatrix4d>::value)
            attr_type = AI_TYPE_MATRIX;
        else if (std::is_same<A, AtMatrix>::value) {
            if (std::is_same<U, GfMatrix4f>::value) 
                same_data = true;
            attr_type = AI_TYPE_MATRIX;
        } else if (std::is_same<A, AtVector>::value) {
            attr_type = AI_TYPE_VECTOR;
            if (std::is_same<U, GfVec3f>::value) // AtVector is represented the same
                                                 // way as GfVec3f
                same_data = true;
        } else if (std::is_same<A, GfVec2f>::value)
            attr_type = AI_TYPE_VECTOR2;
        else if (std::is_same<A, AtVector2>::value) {
            attr_type = AI_TYPE_VECTOR2;
            if (std::is_same<U, GfVec2f>::value) // AtVector2 is represented the
                                                 // same way as GfVec2f
                same_data = true;
        }
    }

    // Call a dedicated function for string conversions
    if (attr_type == AI_TYPE_STRING)
        return exportStringArray(usdAttr, node, attr_name, time);

    bool animated = time.motion_blur && usdAttr.ValueMightBeTimeVarying();

    if (!animated) {
        // Single-key arrays
        VtValue val;
        if (!attr.Get(&val, time.frame))
            return 0;

        const VtArray<U> *array = &(val.Get<VtArray<U>>());

        size_t size = array->size();
        if (size > 0) {
            if (std::is_same<U, GfMatrix4d>::value) {
                // special case for matrices. They're single
                // precision in arnold but double precision in USD,
                // and there is no copy from one to the other.
                VtArray<GfMatrix4d> *arrayMtx = (VtArray<GfMatrix4d> *)(array);
                GfMatrix4d *matrices = arrayMtx->data();
                std::vector<AtMatrix> arnoldVec(size);
                for (size_t v = 0; v < size; ++v) {
                    AtMatrix &aiMat = arnoldVec[v];
                    const double *matArray = matrices[v].GetArray();
                    for (unsigned int i = 0; i < 4; ++i)
                        for (unsigned int j = 0; j < 4; ++j)
                            aiMat[i][j] = matArray[4 * i + j];
                }
                AiNodeSetArray(
                    node, attr_name,
                    AiArrayConvert(size, 1, AI_TYPE_MATRIX, &arnoldVec[0]));

            } else if (same_data) {
                // The USD data representation is the same as the Arnold one, we don't
                // need to convert the data
                AiNodeSetArray(node, attr_name, AiArrayConvert(size, 1, attr_type, array->cdata()));
            } else {
                // Different data representation between USD and Arnold, we need to
                // copy the vector. Note that we could instead allocate the AtArray
                // and set the elements one by one, but I'm assuming it's faster
                // this way
                VtArray<A> arnold_vec;
                arnold_vec.assign(array->cbegin(), array->cend());
                AiNodeSetArray(node, attr_name, AiArrayConvert(size, 1, attr_type, arnold_vec.cdata()));
            }
        } else
            AiNodeResetParameter(node, attr_name);

        return size;
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

        const VtArray<U> *array = &(val.Get<VtArray<U>>());
        
        size_t size = array->size();
        if (size == 0) {
            AiNodeResetParameter(node, attr_name);
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
                VtArray<GfMatrix4d> *arrayMtx = (VtArray<GfMatrix4d> *)(array);
                GfMatrix4d *matrices = arrayMtx->data();

                for (size_t v = 0; v < size; ++v, ++index) {
                    AtMatrix &aiMat = arnoldVec[index];
                    const double *matArray = matrices[v].GetArray();
                    for (unsigned int i = 0; i < 4; ++i)
                        for (unsigned int j = 0; j < 4; ++j)
                            aiMat[i][j] = matArray[4 * i + j];
                }
            }
            AiNodeSetArray(
                node, attr_name,
                AiArrayConvert(size, numKeys, AI_TYPE_MATRIX, arnoldVec.data()));
        } else {
            VtArray<A> arnold_vec;
            arnold_vec.reserve(size * numKeys);
            for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
                if (i > 0) {
                    if (!attr.Get(&val, timeVal)) {
                        return 0;
                    }
                    array = &(val.Get<VtArray<U>>());
                }
                for (const auto& elem : *array) {
                    arnold_vec.push_back(elem);
                }
            }
            AiNodeSetArray(node, attr_name, AiArrayConvert(size, numKeys, attr_type, arnold_vec.data()));
        }
        return size;
    }
}

/**
 *  Export all primvars from this shape, and set them as arnold user data
 *
 **/


// Export the materials / shaders assigned to a shape (node)
void exportMaterialBinding(const UsdPrim& prim, AtNode* node, UsdArnoldReaderContext& context, 
    bool assignDefault = true);

// Export the materials / shaders assigned to a shape (node)
void exportSubsetsMaterialBinding(const UsdPrim& prim, AtNode* node, UsdArnoldReaderContext& context, 
    std::vector<UsdGeomSubset> &subsets, unsigned int elementCount, bool assignDefault = true);

/**
 * Export a specific shader parameter from USD to Arnold
 *
 **/
void exportShaderParameter(
    UsdShadeShader& shader, AtNode* node, const std::string& usdName, const std::string& arnoldName,
    UsdArnoldReaderContext& context);

static inline bool vtValueGetBool(const VtValue& value)
{
    if (value.IsHolding<bool>())
        return value.UncheckedGet<bool>();
    if (value.IsHolding<int>())
        return value.UncheckedGet<int>() != 0;
    if (value.IsHolding<long>())
        return value.UncheckedGet<long>() != 0;
    if (value.IsHolding<VtArray<bool>>())
        return value.UncheckedGet<VtArray<bool>>()[0];
    if (value.IsHolding<VtArray<int>>())
        return value.UncheckedGet<VtArray<int>>()[0] != 0;
    if (value.IsHolding<VtArray<long>>())
        return value.UncheckedGet<VtArray<long>>()[0] != 0;
    return value.Get<bool>();
}
static inline float vtValueGetFloat(const VtValue& value)
{
    if (value.IsHolding<float>())
        return value.UncheckedGet<float>();
    if (value.IsHolding<double>())
        return static_cast<float>(value.UncheckedGet<double>());
    if (value.IsHolding<VtArray<float>>())
        return value.UncheckedGet<VtArray<float>>()[0];
    if (value.IsHolding<VtArray<double>>())
        return static_cast<float>(value.UncheckedGet<VtArray<double>>()[0]);
    
    return value.Get<float>();
}
static inline unsigned char vtValueGetByte(const VtValue& value)
{
    if (value.IsHolding<int>())
        return static_cast<unsigned char>(value.UncheckedGet<int>());
    if (value.IsHolding<long>())
        return static_cast<unsigned char>(value.UncheckedGet<long>());
    if (value.IsHolding<unsigned char>())
        return value.UncheckedGet<unsigned char>();
    if (value.IsHolding<VtArray<unsigned char>>())
        return value.UncheckedGet<VtArray<unsigned char>>()[0];
    if (value.IsHolding<VtArray<int>>())
        return static_cast<unsigned char>(value.UncheckedGet<VtArray<int>>()[0]);
    if (value.IsHolding<VtArray<long>>())
        return static_cast<unsigned char>(value.UncheckedGet<VtArray<long>>()[0]);
        
    return value.Get<unsigned char>();
}
static inline int vtValueGetInt(const VtValue& value)
{
    if (value.IsHolding<int>())
        return value.UncheckedGet<int>();
    if (value.IsHolding<long>())
        return static_cast<int>(value.UncheckedGet<long>());
    if (value.IsHolding<VtArray<int>>())
        return value.UncheckedGet<VtArray<int>>()[0];
    if (value.IsHolding<VtArray<long>>())
        return static_cast<int>(value.UncheckedGet<VtArray<long>>()[0]);

    return value.Get<int>();
}