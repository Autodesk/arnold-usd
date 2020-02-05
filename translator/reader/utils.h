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
    void orient_face_index_attribute(T& attr);
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

// Reverse an attribute of the face. Basically, it converts from the clockwise
// to the counterclockwise and back.
template <class T>
void MeshOrientation::orient_face_index_attribute(T& attr)
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

/** Convert a USD array attribute (type U), to an Arnold array (type A).
 *  When both types are identical, we can simply their pointer to create the
 *array. Otherwise we need to copy the data first
 **/
template <class U, class A>
size_t exportArray(UsdAttribute attr, AtNode* node, const char* attr_name, const TimeSettings& time)
{
    bool same_data = std::is_same<U, A>::value;

    uint8_t attr_type = AI_TYPE_NONE;
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
    else if (std::is_same<A, AtMatrix>::value) {
        if (std::is_same<U, GfMatrix4f>::value) // AtVector is represented the
                                                // same way as GfVec3f
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

    bool animated = time.motion_blur && attr.ValueMightBeTimeVarying();

    if (!animated) {
        // Single-key arrays
        VtArray<U> array;
        if (!attr.Get(&array, time.frame))
            return 0;

        size_t size = array.size();
        if (size > 0) {
            // The USD data representation is the same as the Arnold one, we don't
            // need to convert the data
            if (same_data)
                AiNodeSetArray(node, attr_name, AiArrayConvert(size, 1, attr_type, array.cdata()));
            else {
                // Different data representation between USD and Arnold, we need to
                // copy the vector. Note that we could instead allocate the AtArray
                // and set the elements one by one, but I'm assuming it's faster
                // this way
                VtArray<A> arnold_vec;
                arnold_vec.assign(array.cbegin(), array.cend());
                AiNodeSetArray(node, attr_name, AiArrayConvert(size, 1, attr_type, arnold_vec.cdata()));
            }
        } else
            AiNodeResetParameter(node, attr_name);

        return size;
    } else {
        // Animated array
        GfInterval interval(time.start(), time.end(), false, false);
        std::vector<double> timeSamples;
        attr.GetTimeSamplesInInterval(interval, &timeSamples);
        size_t numKeys = AiMax(int(timeSamples.size()), (int)1);
        numKeys += 2; // need to add the start end end keys (interval has open bounds)

        float timeStep = float(interval.GetMax() - interval.GetMin()) / int(numKeys - 1);
        float timeVal = interval.GetMin();

        VtArray<U> array;
        if (!attr.Get(&array, timeVal))
            return 0;

        size_t size = array.size();
        if (size == 0) {
            AiNodeResetParameter(node, attr_name);
            return 0;
        }
        VtArray<A> arnold_vec;
        arnold_vec.reserve(size * numKeys);
        for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
            if (i > 0) {
                if (!attr.Get(&array, timeVal)) {
                    return 0;
                }
            }
            for (const auto& elem : array) {
                arnold_vec.push_back(elem);
            }
        }
        AiNodeSetArray(node, attr_name, AiArrayConvert(size, numKeys, attr_type, arnold_vec.data()));
        return size;
    }
}

// Export a primvar
template <class T>
bool export_primvar(
    const VtValue& vtValue, const VtIntArray& vtIndices, const TfToken& name, const SdfValueTypeName& typeName,
    const TfToken& interpolation, const UsdPrim& prim, AtNode* node, MeshOrientation* orientation = NULL)
{
    if (!vtValue.IsHolding<VtArray<T>>())
        return false;

    TfToken arnoldName = name;
    bool needDeclare = true;

    // Convert interpolation -> scope
    //
    // USD Interpolation determines how the Primvar interpolates over a
    // geometric primitive:
    // constant One value remains constant over the entire surface
    //          primitive.
    // uniform One value remains constant for each uv patch segment of the
    //         surface primitive (which is a face for meshes).
    // varying Four values are interpolated over each uv patch segment of
    //         the surface. Bilinear interpolation is used for interpolation
    //         between the four values.
    // vertex Values are interpolated between each vertex in the surface
    //        primitive. The basis function of the surface is used for
    //        interpolation between vertices.
    // faceVarying For polygons and subdivision surfaces, four values are
    //             interpolated over each face of the mesh. Bilinear
    //             interpolation is used for interpolation between the four
    //             values.
    //
    // There are four kinds of user-defined data in Arnold:
    //
    // constant constant parameters are data that are defined on a
    //          per-object basis and do not vary across the surface of that
    //          object.
    // uniform uniform parameters are data that are defined on a "per-face"
    //         basis. During subdivision (if appropriate) values are not
    //         interpolated.  Instead, the newly subdivided faces will
    //         contain the value of their "parent" face.
    // varying varying parameters are data that are defined on a per-vertex
    //         basis. During subdivision (if appropriate), the values at the
    //         new vertices are interpolated from the values at the old
    //         vertices. The user should only create parameters of
    //         "interpolatable" variable types (such as floats, colors,
    //         etc.)
    // indexed indexed parameters are data that are defined on a
    //         per-face-vertex basis. During subdivision (if appropriate),
    //         the values at the new vertices are interpolated from the
    //         values at the old vertices, preserving edges where values
    //         were not shared. The user should only create parameters of
    //         "interpolatable" variable types (such as floats, colors,
    //         etc.)
    std::string declaration =
        (interpolation == UsdGeomTokens->uniform)
            ? "uniform "
            : (interpolation == UsdGeomTokens->varying)
                  ? "varying "
                  : (interpolation == UsdGeomTokens->vertex)
                        ? "varying "
                        : (interpolation == UsdGeomTokens->faceVarying) ? "indexed " : "constant ";

    int arnoldAPIType;
    if (std::is_same<T, GfVec2f>::value) {
        declaration += "VECTOR2";
        arnoldAPIType = AI_TYPE_VECTOR2;

        // A special case for UVs.
        if (name == "uv") {
            arnoldName = TfToken("uvlist");
            needDeclare = false;
        }
    } else if (std::is_same<T, GfVec3f>::value) {
        TfToken role = typeName.GetRole();
        if (role == SdfValueRoleNames->Color) {
            declaration += "RGB";
            arnoldAPIType = AI_TYPE_RGB;
        } else {
            declaration += "VECTOR";
            arnoldAPIType = AI_TYPE_VECTOR;
        }
    } else if (std::is_same<T, float>::value) {
        declaration += "FLOAT";
        arnoldAPIType = AI_TYPE_FLOAT;
    } else if (std::is_same<T, int>::value) {
        declaration += "INT";
        arnoldAPIType = AI_TYPE_INT;
    } else {
        // Not supported.
        return false;
    }

    // Declare a user-defined parameter.
    if (needDeclare) {
        AiNodeDeclare(node, arnoldName.GetText(), declaration.c_str());
    }

    // Constant USD attributs are provided as an array of one element.
    if (interpolation == UsdGeomTokens->constant) {
        if (std::is_same<T, GfVec3f>::value) {
            VtArray<GfVec3f> vecArray = vtValue.Get<VtArray<GfVec3f>>();
            GfVec3f value = vecArray[0];

            TfToken role = typeName.GetRole();
            if (role == SdfValueRoleNames->Color) {
                AiNodeSetRGB(node, arnoldName.GetText(), value[0], value[1], value[2]);
            } else {
                AiNodeSetVec(node, arnoldName.GetText(), value[0], value[1], value[2]);
            }
        } else if (std::is_same<T, GfVec2f>::value) {
            auto vector = vtValue.Get<VtArray<GfVec2f>>()[0];
            AiNodeSetVec2(node, arnoldName.GetText(), vector[0], vector[1]);
        } else if (std::is_same<T, float>::value) {
            AiNodeSetFlt(node, arnoldName.GetText(), vtValue.Get<VtArray<float>>()[0]);
        } else if (std::is_same<T, int>::value) {
            AiNodeSetInt(node, arnoldName.GetText(), vtValue.Get<VtArray<int>>()[0]);
        }
    } else {
        const VtArray<T>& rawVal = vtValue.Get<VtArray<T>>();
        AiNodeSetArray(node, arnoldName.GetText(), AiArrayConvert(rawVal.size(), 1, arnoldAPIType, rawVal.data()));

        if (interpolation == UsdGeomTokens->faceVarying) {
            const std::string indexName = name.GetString() + "idxs";
            std::vector<unsigned int> indexes;

            if (vtIndices.empty()) {
                // Arnold doesn't have facevarying iterpolation. It has indexed
                // instead. So it means it's necessary to generate indexes for
                // this type.
                // TODO: Try to generate indexes only once and use it for
                // several primvars.

                indexes.resize(rawVal.size());
                // Fill it with 0, 1, ..., 99.
                std::iota(std::begin(indexes), std::end(indexes), 0);
            } else {
                // We need to use indexes and we can't use vtIndices because we
                // need unsigned int. Converting int to unsigned int.
                indexes.resize(vtIndices.size());
                std::copy(vtIndices.begin(), vtIndices.end(), indexes.begin());
            }

            // If the mesh has left-handed orientation, we need to invert the
            // indices of primvars for each face
            if (orientation)
                orientation->orient_face_index_attribute(indexes);

            AiNodeSetArray(node, indexName.c_str(), AiArrayConvert(indexes.size(), 1, AI_TYPE_UINT, indexes.data()));
        }
    }
    return true;
}

/**
 *  Export all primvars from this shape, and set them as arnold user data
 *
 **/
void exportPrimvars(const UsdPrim& prim, AtNode* node, const TimeSettings& time, MeshOrientation* orientation = NULL);

// Export the materials / shaders assigned to a shape (node)
void exportMaterialBinding(const UsdPrim& prim, AtNode* node, UsdArnoldReaderContext& context);

/**
 * Export a specific shader parameter from USD to Arnold
 *
 **/
void exportParameter(
    UsdShadeShader& shader, AtNode* node, const std::string& usdName, const std::string& arnoldName,
    UsdArnoldReaderContext& context);

