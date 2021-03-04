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
#include "read_geometry.h"
#include "registry.h"

#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/curves.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdVol/openVDBAsset.h>
#include <pxr/usd/usdVol/volume.h>

#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/transform.h>
#include <pxr/base/tf/stringUtils.h>

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "utils.h"
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE
static AtString s_matrixStr("matrix");
    
namespace {

/** 
 * Read a UsdGeomPointsBased points attribute to get its positions, as well as its velocities
 * If velocities are found, we just get the positions at the "current" frame, and interpolate
 * to compute the positions keys.
 * If no velocities are found, we get the positions at the different motion steps
 **/
static inline void _ReadPointsAndVelocities(const UsdGeomPointBased &geom, AtNode *node, 
                                        const char *attrName, const TimeSettings &time)
{
    UsdAttribute pointsAttr = geom.GetPointsAttr();
    UsdAttribute velAttr = geom.GetVelocitiesAttr();

    VtValue velValue;
    if (time.motionBlur && velAttr && velAttr.Get(&velValue, time.frame)) {
        // Motion blur is enabled and velocity attribute is present
        const VtArray<GfVec3f>& velArray = velValue.Get<VtArray<GfVec3f>>();
        size_t velSize = velArray.size();
        if (velSize > 0) {
            // Non-empty velocities
            VtValue posValue;
            if (pointsAttr.Get(&posValue, time.frame)) {
                const VtArray<GfVec3f>& posArray = posValue.Get<VtArray<GfVec3f>>();
                size_t posSize = posArray.size();
                // Only consider velocities if they're the same size as positions
                if (posSize == velSize) {
                    VtArray<GfVec3f> fullVec;
                    fullVec.resize(2 * posSize); // we just want 2 motion keys
                    const GfVec3f *pos = posArray.data();
                    const GfVec3f *vel = velArray.data();
                    for (size_t i = 0; i < posSize; ++i, pos++, vel++) {
                        // Set 2 keys, the first one will be the extrapolated
                        // position at "shutter start", and the second the 
                        // extrapolated position at "shutter end", based
                        // on the velocities
                        fullVec[i] = (*pos) + time.motionStart * (*vel);
                        fullVec[i + posSize] = (*pos) + time.motionEnd * (*vel);
                    }
                    // Set the arnold array attribute
                    AiNodeSetArray(node, attrName, AiArrayConvert(posSize, 2, 
                                    AI_TYPE_VECTOR, fullVec.data()));
                    // We need to set the motion start and motion end
                    // corresponding the array keys we've just set
                    AiNodeSetFlt(node, "motion_start", time.motionStart);
                    AiNodeSetFlt(node, "motion_end", time.motionEnd);
                    return;
                }
            }
        }
    }

    // No velocities, let's read the positions, eventually at different motion frames
    if (ReadArray<GfVec3f, GfVec3f>(pointsAttr, node, attrName, time) > 1) {
        // We got more than 1 key, so we need to set the motion start/end
        AiNodeSetFlt(node, "motion_start", time.motionStart);
        AiNodeSetFlt(node, "motion_end", time.motionEnd);
    }
}

} // namespace

/** Reading a USD Mesh description to Arnold
 **/
void UsdArnoldReadMesh::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;
    
    // For some attributes, we should never try to read them with motion blur, 
    // we use another timeSettings for them
    TimeSettings staticTime(time);
    staticTime.motionBlur = false;

    AtNode *node = context.CreateArnoldNode("polymesh", prim.GetPath().GetText());

    AiNodeSetBool(node, "smoothing", true);

    // Get mesh.
    UsdGeomMesh mesh(prim);

    MeshOrientation meshOrientation;
    // Get orientation. If Left-handed, we will need to invert the vertex
    // indices
    {
        TfToken orientationToken;
        if (mesh.GetOrientationAttr().Get(&orientationToken)) {
            if (orientationToken == UsdGeomTokens->leftHanded) {
                meshOrientation.reverse = true;
                mesh.GetFaceVertexCountsAttr().Get(&meshOrientation.nsidesArray, frame);
            }
        }
    }
    ReadArray<int, unsigned char>(mesh.GetFaceVertexCountsAttr(), node, "nsides", staticTime);

    if (!meshOrientation.reverse) {
        // Basic right-handed orientation, no need to do anything special here
        ReadArray<int, unsigned int>(mesh.GetFaceVertexIndicesAttr(), node, "vidxs", staticTime);
    } else {
        // We can't call ReadArray here because the orientation requires to
        // reverse face attributes. So we're duplicating the function here.
        VtIntArray array;
        mesh.GetFaceVertexIndicesAttr().Get(&array, frame);
        size_t size = array.size();
        if (size > 0) {
            meshOrientation.OrientFaceIndexAttribute(array);

            // Need to convert the data from int to unsigned int
            std::vector<unsigned int> arnold_vec(array.begin(), array.end());
            AiNodeSetArray(node, "vidxs", AiArrayConvert(size, 1, AI_TYPE_UINT, arnold_vec.data()));
        } else
            AiNodeResetParameter(node, "vidxs");
    }

    _ReadPointsAndVelocities(mesh, node, "vlist", time);
    
    VtValue sidednessValue;
    if (mesh.GetDoubleSidedAttr().Get(&sidednessValue))
        AiNodeSetByte(node, "sidedness", VtValueGetBool(sidednessValue) ? AI_RAY_ALL : 0);

    // reset subdiv_iterations to 0, it might be set in readArnoldParameter
    AiNodeSetByte(node, "subdiv_iterations", 0);
    ReadMatrix(prim, node, time, context);

    ReadPrimvars(prim, node, time, context, &meshOrientation);

    std::vector<UsdGeomSubset> subsets = UsdGeomSubset::GetAllGeomSubsets(mesh);

    if (!subsets.empty()) {
        // Currently, subsets are only used for shader & disp_map assignments
        VtIntArray faceVtxArray;
        mesh.GetFaceVertexCountsAttr().Get(&faceVtxArray, time.frame);
        ReadSubsetsMaterialBinding(prim, node, context, subsets, faceVtxArray.size());
    } else {
        ReadMaterialBinding(prim, node, context);
    }

    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check if subdiv_iterations were set in _ReadArnoldParameters,
    // and only set the subdiv_type if it's > 0. If we don't do this,
    // we get smoothed normals by default
    if (AiNodeGetByte(node, "subdiv_iterations") > 0) {
        TfToken subdiv;
        mesh.GetSubdivisionSchemeAttr().Get(&subdiv);
        if (subdiv == UsdGeomTokens->none)
            AiNodeSetStr(node, "subdiv_type", "none");
        else if (subdiv == UsdGeomTokens->catmullClark)
            AiNodeSetStr(node, "subdiv_type", "catclark");
        else if (subdiv == UsdGeomTokens->bilinear)
            AiNodeSetStr(node, "subdiv_type", "linear");
        else
            AiMsgWarning(
                "[usd] %s subdivision scheme not supported for mesh on path %s", subdiv.GetString().c_str(),
                mesh.GetPath().GetString().c_str());
    }

    // Check the prim visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, "visibility", 0);
}

void UsdArnoldReadCurves::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    AtNode *node = context.CreateArnoldNode("curves", prim.GetPath().GetText());

    int basis = 3;
    if (prim.IsA<UsdGeomBasisCurves>()) {
        // TODO: use a scope_pointer for curves and basisCurves.
        UsdGeomBasisCurves basisCurves(prim);
        TfToken curveType;
        basisCurves.GetTypeAttr().Get(&curveType, frame);
        if (curveType == UsdGeomTokens->cubic) {
            TfToken basisType;
            basisCurves.GetBasisAttr().Get(&basisType, frame);

            if (basisType == UsdGeomTokens->bezier)
                basis = 0;
            else if (basisType == UsdGeomTokens->bspline)
                basis = 1;
            else if (basisType == UsdGeomTokens->catmullRom)
                basis = 2;
        }
    }
    AiNodeSetInt(node, "basis", basis);

    UsdGeomCurves curves(prim);
    // CV counts per curve
    ReadArray<int, unsigned int>(curves.GetCurveVertexCountsAttr(), node, "num_points", time);
    // CVs positions

    _ReadPointsAndVelocities(curves, node, "points", time);

    AtArray *pointsArray = AiNodeGetArray(node, "points");
    unsigned int pointsSize = (pointsArray) ? AiArrayGetNumElements(pointsArray) : 0;

    // Widths
    // We need to divide the width by 2 in order to get the radius for arnold points
    VtArray<float> widthArray;
    if (curves.GetWidthsAttr().Get(&widthArray, time.frame)) {
        size_t widthCount = widthArray.size();
        if (widthCount <= 1 && pointsSize > widthCount) {
            // USD accepts empty width attributes, or a constant width for all points,
            // but arnold fails in that case. So we need to generate a dedicated array
            float radiusVal = (widthCount == 0) ? 0.f : widthArray[0] * 0.5f;
            // Create an array where each point has the same radius
            std::vector<float> radiusVec(pointsSize, radiusVal);
            AiNodeSetArray(node, "radius", AiArrayConvert(pointsSize, 1, AI_TYPE_FLOAT, &radiusVec[0]));
        } else if (widthCount > 0) {
            // TODO: Usd curves support vertex interpolation for the widths, but arnold doesn't
            // (see #239)
            AtArray *radiusArray = AiArrayAllocate(widthCount, 1, AI_TYPE_FLOAT);
            float *out = static_cast<float *>(AiArrayMap(radiusArray));
            for (unsigned int i = 0; i < widthCount; ++i) {
                out[i] = widthArray[i] * 0.5f;
            }
            AiArrayUnmap(radiusArray);
            AiNodeSetArray(node, "radius", radiusArray);
        }
    }

    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    std::vector<UsdGeomSubset> subsets = UsdGeomSubset::GetAllGeomSubsets(curves);

    if (!subsets.empty()) {
        // Currently, subsets are only used for shader & disp_map assignments
        VtIntArray curveVtxArray;
        curves.GetCurveVertexCountsAttr().Get(&curveVtxArray, time.frame);
        ReadSubsetsMaterialBinding(prim, node, context, subsets, curveVtxArray.size());
    } else {
        ReadMaterialBinding(prim, node, context);
    }

    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the prim visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, "visibility", 0);
}

void UsdArnoldReadPoints::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    AtNode *node = context.CreateArnoldNode("points", prim.GetPath().GetText());

    UsdGeomPoints points(prim);

    // Points positions
    _ReadPointsAndVelocities(points, node, "points", time);

    AtArray *pointsArray = AiNodeGetArray(node, "points");
    unsigned int pointsSize = (pointsArray) ? AiArrayGetNumElements(pointsArray) : 0;

    // Points radius
    // We need to divide the width by 2 in order to get the radius for arnold points
    VtArray<float> widthArray;
    if (points.GetWidthsAttr().Get(&widthArray, time.frame)) {
        size_t widthCount = widthArray.size();
        if (widthCount <= 1 && pointsSize > widthCount) {
            // USD accepts empty width attributes, or a constant width for all points,
            // but arnold fails in that case. So we need to generate a dedicated array
            float radiusVal = (widthCount == 0) ? 0.f : widthArray[0] * 0.5f;
            // Create an array where each point has the same radius
            std::vector<float> radiusVec(pointsSize, radiusVal);
            AiNodeSetArray(node, "radius", AiArrayConvert(pointsSize, 1, AI_TYPE_FLOAT, &radiusVec[0]));
        } else if (widthCount > 0) {
            AtArray *radiusArray = AiArrayAllocate(widthCount, 1, AI_TYPE_FLOAT);
            float *out = static_cast<float *>(AiArrayMap(radiusArray));
            for (unsigned int i = 0; i < widthCount; ++i) {
                out[i] = widthArray[i] * 0.5f;
            }
            AiArrayUnmap(radiusArray);
            AiNodeSetArray(node, "radius", radiusArray);
        }
    }

    ReadMatrix(prim, node, time, context);

    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");
    // Check the primitive visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, "visibility", 0);
}

/**
 *   Convert the basic USD shapes (cube, sphere, cylinder, cone,...)
 *   to Arnold. There are 2 main differences so far :
 *      - capsules don't exist in arnold
 *      - cylinders are different (one is closed the other isn't)
 **/
void UsdArnoldReadCube::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;
    AtNode *node = context.CreateArnoldNode("box", prim.GetPath().GetText());
    UsdGeomCube cube(prim);

    VtValue sizeValue;
    if (cube.GetSizeAttr().Get(&sizeValue)) {
        float size = VtValueGetFloat(sizeValue);
        AiNodeSetVec(node, "min", -size / 2.f, -size / 2.f, -size / 2.f);
        AiNodeSetVec(node, "max", size / 2.f, size / 2.f, size / 2.f);
    }

    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, "visibility", 0);
}

void UsdArnoldReadSphere::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;
    AtNode *node = context.CreateArnoldNode("sphere", prim.GetPath().GetText());
    UsdGeomSphere sphere(prim);

    VtValue radiusValue;
    if (sphere.GetRadiusAttr().Get(&radiusValue))
        AiNodeSetFlt(node, "radius", VtValueGetFloat(radiusValue));

    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, "visibility", 0);
}

// Conversion code that is common to cylinder, cone and capsule
template <class T>
void exportCylindricalShape(const UsdPrim &prim, AtNode *node, const char *radiusName)
{
    T geom(prim);

    VtValue radiusValue;
    if (geom.GetRadiusAttr().Get(&radiusValue))
        AiNodeSetFlt(node, radiusName, VtValueGetFloat(radiusValue));

    float height = 1.f;
    VtValue heightValue;
    if (geom.GetHeightAttr().Get(&heightValue))
        height = VtValueGetFloat(heightValue);

    height /= 2.f;

    TfToken axis;
    geom.GetAxisAttr().Get(&axis);
    AtVector bottom(0.f, 0.f, 0.f);
    AtVector top(0.f, 0.f, 0.f);

    if (axis == UsdGeomTokens->x) {
        bottom.x = -height;
        top.x = height;
    } else if (axis == UsdGeomTokens->y) {
        bottom.y = -height;
        top.y = height;
    } else // UsdGeomTokens->z or unknown
    {
        bottom.z = -height;
        top.z = height;
    }
    AiNodeSetVec(node, "bottom", bottom.x, bottom.y, bottom.z);
    AiNodeSetVec(node, "top", top.x, top.y, top.z);
}

void UsdArnoldReadCylinder::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("cylinder", prim.GetPath().GetText());

    exportCylindricalShape<UsdGeomCylinder>(prim, node, "radius");
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, "visibility", 0);
}

void UsdArnoldReadCone::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("cone", prim.GetPath().GetText());

    exportCylindricalShape<UsdGeomCone>(prim, node, "bottom_radius");

    const TimeSettings &time = context.GetTimeSettings();
    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetByte(node, "visibility", 0);
}

// Note that we don't have capsule shapes in Arnold. Do we want to make a
// special case, and combine cylinders with spheres, or is it enough for now ?
void UsdArnoldReadCapsule::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("cylinder", prim.GetPath().GetText());

    exportCylindricalShape<UsdGeomCapsule>(prim, node, "radius");
    const TimeSettings &time = context.GetTimeSettings();
    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetByte(node, "visibility", 0);
}

void ApplyInputMatrix(AtNode *node, const AtParamValueMap* params)
{
    if (params == nullptr)
        return;
    AtArray* parentMatrices = nullptr;
    if (!AiParamValueMapGetArray(params, s_matrixStr, &parentMatrices))
        return;
    if (parentMatrices == nullptr || AiArrayGetNumElements(parentMatrices) == 0)
        return;

    AtArray *matrix = AiNodeGetArray(node, s_matrixStr);
    AtMatrix m;
    if (matrix != nullptr && AiArrayGetNumElements(matrix) > 0) 
        m = AiM4Mult(AiArrayGetMtx(parentMatrices, 0), AiArrayGetMtx(matrix, 0));
    else
        m = AiArrayGetMtx(parentMatrices, 0);
    
    AiArraySetMtx(matrix, 0, m);
}

void UsdArnoldReadBounds::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    if (!context.GetPrimVisibility(prim, frame))
        return;

    AtNode *node = context.CreateArnoldNode("box", prim.GetPath().GetText());
    if (!prim.IsA<UsdGeomBoundable>())
        return;

    UsdGeomBoundable boundable(prim);
    VtVec3fArray extent;

    UsdGeomBoundable::ComputeExtentFromPlugins(boundable, UsdTimeCode(frame), &extent);

    AiNodeSetVec(node, "min", extent[0][0], extent[0][1], extent[0][2]);
    AiNodeSetVec(node, "max", extent[1][0], extent[1][1], extent[1][2]);
    ReadMatrix(prim, node, time, context);
    ApplyInputMatrix(node, _params);

    // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, "visibility", 0);
}

void UsdArnoldReadGenericPolygons::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    if (!context.GetPrimVisibility(prim, frame))
        return;

    AtNode *node = context.CreateArnoldNode("polymesh", prim.GetPath().GetText());

    if (!prim.IsA<UsdGeomMesh>())
        return;

    UsdGeomMesh mesh(prim);
    MeshOrientation meshOrientation;
    // Get orientation. If Left-handed, we will need to invert the vertex
    // indices
    {
        TfToken orientationToken;
        if (mesh.GetOrientationAttr().Get(&orientationToken)) {
            if (orientationToken == UsdGeomTokens->leftHanded) {
                meshOrientation.reverse = true;
                mesh.GetFaceVertexCountsAttr().Get(&meshOrientation.nsidesArray, frame);
            }
        }
    }
    ReadArray<int, unsigned char>(mesh.GetFaceVertexCountsAttr(), node, "nsides", time);

    if (!meshOrientation.reverse) {
        // Basic right-handed orientation, no need to do anything special here
        ReadArray<int, unsigned int>(mesh.GetFaceVertexIndicesAttr(), node, "vidxs", time);
    } else {
        // We can't call ReadArray here because the orientation requires to
        // reverse face attributes. So we're duplicating the function here.
        VtIntArray array;
        mesh.GetFaceVertexIndicesAttr().Get(&array, frame);
        size_t size = array.size();
        if (size > 0) {
            meshOrientation.OrientFaceIndexAttribute(array);

            // Need to convert the data from int to unsigned int
            std::vector<unsigned int> arnold_vec(array.begin(), array.end());
            AiNodeSetArray(node, "vidxs", AiArrayConvert(size, 1, AI_TYPE_UINT, arnold_vec.data()));
        } else
            AiNodeResetParameter(node, "vidxs");
    }
    ReadArray<GfVec3f, GfVec3f>(mesh.GetPointsAttr(), node, "vlist", time);
    ReadMatrix(prim, node, time, context);
    ApplyInputMatrix(node, _params);

    // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, "visibility", 0);
}

void UsdArnoldReadGenericPoints::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    AtNode *node = context.CreateArnoldNode("points", prim.GetPath().GetText());

    if (!prim.IsA<UsdGeomPointBased>())
        return;

    UsdGeomPointBased points(prim);
    ReadArray<GfVec3f, GfVec3f>(points.GetPointsAttr(), node, "points", time);
    ReadMatrix(prim, node, time, context);
    ApplyInputMatrix(node, _params);

    // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, "visibility", 0);
}

/**
 *    Convert the Point Instancer node to Arnold. Since there is no such node in Arnold (yet),
 *    we need to convert it as ginstances, one for each instance.
 *    There are however certain use case that are more complex :
 *        - a point instancer instantiating another point instancer (how to handle the recursion ?)
 *        - one of the "proto nodes" to be instantiated is a Xform in the middle of the hierarchy, and thus doesn't
 match an existing arnold node (here we'd need to create one ginstance per leaf node below this xform)
 *
 *     A simple way to address these issues, is to check if each "proto node" exists in the Arnold scene
 *     or not. If it doesn't, then we create a usd procedural with object_path pointing at this path. This way,
 *     each instance of this usd procedural will properly instantiate the whole contents of this path.
 **/

void UsdArnoldReadPointInstancer::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    // If the USD primitive is hidden, we need to hide each of the nodes that are being created here
    bool isVisible = context.GetPrimVisibility(prim, frame);

    UsdGeomPointInstancer pointInstancer(prim);

    // this will be used later to contruct the name of the instances
    std::string primName = prim.GetPath().GetText();

    // get all proto paths (i.e. input nodes to be instantiated)
    SdfPathVector protoPaths;
    pointInstancer.GetPrototypesRel().GetTargets(&protoPaths);

    // get the usdFilePath from the reader, we will use this path later to apply when we create new usd procs
    std::string filename = context.GetReader()->GetFilename();

    // Same as above, get the eventual overrides from the reader
    const AtArray *overrides = context.GetReader()->GetOverrides();

    // get proto type index for all instances
    VtIntArray protoIndices;
    pointInstancer.GetProtoIndicesAttr().Get(&protoIndices, frame);

    for (size_t i = 0; i < protoPaths.size(); ++i) {
        const SdfPath &protoPath = protoPaths.at(i);
        // get the proto primitive, and ensure it's properly exported to arnold,
        // since we don't control the order in which nodes are read.
        UsdPrim protoPrim = context.GetReader()->GetStage()->GetPrimAtPath(protoPath);
        std::string objType = (protoPrim) ? protoPrim.GetTypeName().GetText() : "";

        // I need to create a new proto node in case this primitive isn't directly translated as an Arnold AtNode.
        // As of now, this only happens for Xform and Point Instancer nodes, so I'm checking for these types,
        // and also I'm verifying if the registry is able to read nodes of this type.
        // In the future we might want to make this more robust, we could eventually add a function in
        // the primReader telling us if this primitive will generate an arnold node with the same name or not.
        bool createProto =
            (objType == "Xform" || objType == "PointInstancer" || objType == "" ||
             (context.GetReader()->GetRegistry()->GetPrimReader(objType) == nullptr));

        if (createProto) {
            // There's no AtNode for this proto, we need to create a usd procedural that loads
            // the same usd file but points only at this object path
            AtNode *node = context.CreateArnoldNode("usd", protoPath.GetText());

            AiNodeSetStr(node, "filename", filename.c_str());
            AiNodeSetStr(node, "object_path", protoPath.GetText());
            AiNodeSetFlt(node, "frame", frame); // give it the desired frame
            AiNodeSetFlt(node, "motion_start", time.motionStart);
            AiNodeSetFlt(node, "motion_end", time.motionEnd);
            if (overrides)
                AiNodeSetArray(node, "overrides", AiArrayCopy(overrides));

            if (!isVisible)
                AiNodeSetByte(node, "visibility", 0);
        }
    }
    std::vector<UsdTimeCode> times;
    if (time.motionBlur) {
        times.push_back(time.start());
        times.push_back(time.end());
    } else {
        times.push_back(frame);
    }
    std::vector<bool> pruneMaskValues = pointInstancer.ComputeMaskAtTime(frame);
    if (!pruneMaskValues.empty() && pruneMaskValues.size() != protoIndices.size()) {
        // If the amount of prune mask elements doesn't match the amount of instances,
        // then something is wrong. We dump an error and clear the mask vector.
        AiMsgError("[usd] Point instancer %s : Mismatch in length of indices and mask", primName.c_str());
        pruneMaskValues.clear();
    }

    std::vector<VtArray<GfMatrix4d> > xformsArray;
    pointInstancer.ComputeInstanceTransformsAtTimes(&xformsArray, times, frame);

    for (size_t i = 0; i < protoIndices.size(); ++i) {
        // This instance has to be pruned, let's skip it
        if (!pruneMaskValues.empty() && pruneMaskValues[i] == false)
            continue;

        std::vector<AtMatrix> matrices(xformsArray.size());
        // loop over all the motion steps and append the matrices as a big list of floats

        for (size_t t = 0; t < xformsArray.size(); ++t) {
            const double *matrixArray = xformsArray[t][i].GetArray();
            AtMatrix &matrix = matrices[t];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j, matrixArray++)
                    matrix[i][j] = (float)*matrixArray;
        }

        // construct the instance name, based on the point instancer name,
        // suffixed by the instance number
        std::string instanceName = TfStringPrintf("%s_%d", primName.c_str(), i);

        // create a ginstance pointing at this proto node
        AtNode *arnoldInstance = context.CreateArnoldNode("ginstance", instanceName.c_str());

        AiNodeSetBool(arnoldInstance, "inherit_xform", false);
        int protoId = protoIndices[i]; // which proto to instantiate

        // Add a connection from ginstance.node to the desired proto. This connection will be applied
        // after all nodes were exported to Arnold.
        if (protoId < protoPaths.size()) // safety out-of-bounds check, shouldn't happen
            context.AddConnection(
                arnoldInstance, "node", protoPaths.at(protoId).GetText(), UsdArnoldReaderContext::CONNECTION_PTR);
        AiNodeSetFlt(arnoldInstance, "motion_start", time.motionStart);
        AiNodeSetFlt(arnoldInstance, "motion_end", time.motionEnd);
        // set the instance xform
        AiNodeSetArray(arnoldInstance, "matrix", AiArrayConvert(1, matrices.size(), AI_TYPE_MATRIX, &matrices[0]));
        // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
        // Otherwise, force it to be visible to all rays, because the proto might be hidden
        if (!isVisible)
            AiNodeSetByte(arnoldInstance, "visibility", 0);
        else
            AiNodeSetByte(arnoldInstance, "visibility", AI_RAY_ALL);
    }
}
void UsdArnoldReadVolume::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.CreateArnoldNode("volume", prim.GetPath().GetText());
    UsdVolVolume volume(prim);
    const TimeSettings &time = context.GetTimeSettings();

    UsdVolVolume::FieldMap fields = volume.GetFieldPaths();
    std::string filename;
    std::vector<std::string> grids;

    // Loop over all the fields in this volume node.
    // Note that arnold doesn't support grids from multiple vdb files, as opposed to USD volumes.
    // So we can only use the first .vdb that is found, and we'll dump a warning if needed.
    for (UsdVolVolume::FieldMap::iterator it = fields.begin(); it != fields.end(); ++it) {
        UsdPrim fieldPrim = context.GetReader()->GetStage()->GetPrimAtPath(it->second);
        if (!fieldPrim.IsA<UsdVolOpenVDBAsset>())
            continue;
        UsdVolOpenVDBAsset vdbAsset(fieldPrim);

        VtValue vdbFilePathValue;

        if (vdbAsset.GetFilePathAttr().Get(&vdbFilePathValue)) {
            std::string fieldFilename = VtValueGetString(vdbFilePathValue);
            if (filename.empty())
                filename = fieldFilename;
            else if (fieldFilename != filename) {
                AiMsgWarning("[usd] %s: arnold volume nodes only support a single .vdb file. ", AiNodeGetName(node));
            }
            TfToken vdbGrid;
            if (vdbAsset.GetFieldNameAttr().Get(&vdbGrid)) {
                grids.push_back(vdbGrid);
            }
        }
    }

    // Now set the first vdb filename that was found
    AiNodeSetStr(node, "filename", AtString(filename.c_str()));

    // Set all the grids that are needed
    AtArray *gridsArray = AiArrayAllocate(grids.size(), 1, AI_TYPE_STRING);
    for (size_t i = 0; i < grids.size(); ++i) {
        AiArraySetStr(gridsArray, i, AtString(grids[i].c_str()));
    }
    AiNodeSetArray(node, "grids", gridsArray);

    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context, false); // don't assign the default shader

    _ReadArnoldParameters(prim, context, node, time, "primvars:arnold");
    // Check the prim visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetByte(node, "visibility", 0);
}

void UsdArnoldReadProceduralCustom::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    // This schema is meant for custom procedurals. Its attribute "node_entry" will
    // indicate what is the node entry name for this node.
    UsdAttribute attr = prim.GetAttribute(TfToken("arnold:node_entry"));
    // for backward compatibility, check the attribute without namespace
    if (!attr)
        attr = prim.GetAttribute(TfToken("node_entry"));
    
    VtValue value;
    // If the attribute "node_entry" isn't defined, we don't know what type of node
    // to create, so there is nothing we can do
    if (!attr || !attr.Get(&value)) {
        return;
    }

    std::string nodeType = VtValueGetString(value);
    AtNode *node = context.CreateArnoldNode(nodeType.c_str(), prim.GetPath().GetText());

    const TimeSettings &time = context.GetTimeSettings();
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context, false); // don't assign the default shader
    // The attributes will be read here, without an arnold scope, as in UsdArnoldReadArnoldType
    _ReadArnoldParameters(prim, context, node, time, "arnold", true);

    // Check the prim visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, time.frame)) {
        AiNodeSetByte(node, "visibility", 0);
    }
}

void UsdArnoldReadProcViewport::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtUniverse *universe = context.GetReader()->GetUniverse();
    const TimeSettings &time = context.GetTimeSettings();

    std::string filename;
    std::string nodeType = _procName;

    if (!_procName.empty()) {
        // Get the filename of this ass/usd/abc procedural
        UsdAttribute attr = prim.GetAttribute(TfToken("arnold:filename"));
        // for backward compatibility, check the attribute without namespace
        if (!attr)
            attr = prim.GetAttribute(TfToken("filename"));

        VtValue value;

        if (!attr || !attr.Get(&value)) {
            return;
        }

        filename = VtValueGetString(value);
    } else {
        // There's not a determined procedural node type, this is a custom procedural.
        // We get this information from the attribute "node_entry"
        UsdAttribute attr = prim.GetAttribute(TfToken("arnold:node_entry"));
        // for backward compatibility, check the attribute without namespace
        if (!attr)
            attr = prim.GetAttribute(TfToken("node_entry"));

        VtValue value;
        if (!attr || !attr.Get(&value)) {
            return;
        }

        nodeType = VtValueGetString(value);
    }

    // create a temporary universe to create a dummy procedural
    AtUniverse *tmpUniverse = AiUniverse();

    // copy the procedural search path string from the input universe
    AiNodeSetStr(
        AiUniverseGetOptions(tmpUniverse), "procedural_searchpath",
        AiNodeGetStr(AiUniverseGetOptions(universe), "procedural_searchpath"));

    // Create a procedural with the given node type
    AtNode *proc = AiNode(tmpUniverse, nodeType.c_str(), "viewport_proc");

    // Set the eventual filename
    if (!filename.empty()) {
        AiNodeSetStr(proc, "filename", filename.c_str());
    }
    // read the matrix and apply the eventual input one from the AtParamsValueMap
    // This node's matrix won't be taken into account but we'll apply it to the params map
    ReadMatrix(prim, proc, time, context);
    ApplyInputMatrix(proc, _params);
    AtMatrix m;
    bool setMatrixParam = false;
    AtArray *matrices = AiNodeGetArray(proc, s_matrixStr);
    if (matrices && AiArrayGetNumElements(matrices) > 0) 
        setMatrixParam = (!AiM4IsIdentity(AiArrayGetMtx(matrices, 0)));
    
    // ensure we read all the parameters from the procedural
    _ReadArnoldParameters(prim, context, proc, time, "arnold", true);
    ReadPrimvars(prim, proc, time, context);

    AtParamValueMap *params = 
            (_params) ? AiParamValueMapClone(_params) : AiParamValueMap();
    AiParamValueMapSetInt(params, AtString("mask"), AI_NODE_SHAPE);
    // if needed, propagate the matrix to the child nodes
    if (setMatrixParam)
        AiParamValueMapSetArray(params, s_matrixStr, matrices);

    AiProceduralViewport(proc, universe, _mode, params);
    AiParamValueMapDestroy(params);

    AiUniverseDestroy(tmpUniverse);
}
