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

#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/curves.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/sphere.h>

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "utils.h"
//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

/** Exporting a USD Mesh description to Arnold
 * TODO: - what to do with UVs ?
 **/
void UsdArnoldReadMesh::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("polymesh", prim.GetPath().GetText());
    
    AiNodeSetBool(node, "smoothing", true);
    const TimeSettings &time = context.getTimeSettings();
    float frame = time.frame;

    // Get mesh.
    UsdGeomMesh mesh(prim);

    MeshOrientation mesh_orientation;
    // Get orientation. If Left-handed, we will need to invert the vertex
    // indices
    {
        TfToken orientation_token;
        if (mesh.GetOrientationAttr().Get(&orientation_token)) {
            if (orientation_token == UsdGeomTokens->leftHanded) {
                mesh_orientation.reverse = true;
                mesh.GetFaceVertexCountsAttr().Get(&mesh_orientation.nsides_array, frame);
            }
        }
    }
    exportArray<int, unsigned char>(mesh.GetFaceVertexCountsAttr(), node, "nsides", time);

    if (!mesh_orientation.reverse) {
        // Basic right-handed orientation, no need to do anything special here
        exportArray<int, unsigned int>(mesh.GetFaceVertexIndicesAttr(), node, "vidxs", time);
    } else {
        // We can't call exportArray here because the orientation requires to
        // reverse face attributes. So we're duplicating the function here.
        VtIntArray array;
        mesh.GetFaceVertexIndicesAttr().Get(&array, frame);
        size_t size = array.size();
        if (size > 0) {
            mesh_orientation.orient_face_index_attribute(array);

            // Need to convert the data from int to unsigned int
            std::vector<unsigned int> arnold_vec(array.begin(), array.end());
            AiNodeSetArray(node, "vidxs", AiArrayConvert(size, 1, AI_TYPE_UINT, arnold_vec.data()));
        } else
            AiNodeResetParameter(node, "vidxs");
    }

    // Vertex positions
    exportArray<GfVec3f, GfVec3f>(mesh.GetPointsAttr(), node, "vlist", time);

    VtValue sidedness;
    if (mesh.GetDoubleSidedAttr().Get(&sidedness))
        AiNodeSetByte(node, "sidedness", sidedness.Get<bool>() ? AI_RAY_ALL : 0);

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

    AiNodeSetByte(node, "subdiv_iterations", 0);
    exportMatrix(prim, node, time, context);

    exportPrimvars(prim, node, time, &mesh_orientation);
    exportMaterialBinding(prim, node, context);

    readArnoldParameters(prim, context, node, time, "primvars:arnold");
}

void UsdArnoldReadCurves::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("curves", prim.GetPath().GetText());
    
    const TimeSettings &time = context.getTimeSettings();
    float frame = time.frame;

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
    exportArray<int, unsigned int>(curves.GetCurveVertexCountsAttr(), node, "num_points", time);
    // CVs positions
    exportArray<GfVec3f, GfVec3f>(curves.GetPointsAttr(), node, "points", time);
    // Widths
    exportArray<float, float>(curves.GetWidthsAttr(), node, "radius", time);

    exportMatrix(prim, node, time, context);
    exportPrimvars(prim, node, time);
    exportMaterialBinding(prim, node, context);

    readArnoldParameters(prim, context, node, time, "primvars:arnold");
}

void UsdArnoldReadPoints::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("points", prim.GetPath().GetText());
    
    UsdGeomPoints points(prim);
    const TimeSettings &time = context.getTimeSettings();
    float frame = time.frame;

    // Points positions
    exportArray<GfVec3f, GfVec3f>(points.GetPointsAttr(), node, "points", time);
    // Points radius
    exportArray<float, float>(points.GetWidthsAttr(), node, "radius", time);

    exportMatrix(prim, node, time, context);

    exportPrimvars(prim, node, time);
    exportMaterialBinding(prim, node, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");
}

/**
 *   Convert the basic USD shapes (cube, sphere, cylinder, cone,...)
 *   to Arnold. There are 2 main differences so far :
 *      - capsules don't exist in arnold
 *      - cylinders are different (one is closed the other isn't)
 **/
void UsdArnoldReadCube::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("box", prim.GetPath().GetText());
    
    UsdGeomCube cube(prim);
    const TimeSettings &time = context.getTimeSettings();
    float frame = time.frame;

    VtValue size_attr;
    if (cube.GetSizeAttr().Get(&size_attr)) {
        float size_value = (float)size_attr.Get<double>();
        AiNodeSetVec(node, "min", -size_value / 2.f, -size_value / 2.f, -size_value / 2.f);
        AiNodeSetVec(node, "max", size_value / 2.f, size_value / 2.f, size_value / 2.f);
    }

    exportMatrix(prim, node, time, context);
    exportPrimvars(prim, node, time);
    exportMaterialBinding(prim, node, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");
}

void UsdArnoldReadSphere::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("sphere", prim.GetPath().GetText());
    
    UsdGeomSphere sphere(prim);
    const TimeSettings &time = context.getTimeSettings();
    float frame = time.frame;

    VtValue radius_attr;
    if (sphere.GetRadiusAttr().Get(&radius_attr))
        AiNodeSetFlt(node, "radius", (float)radius_attr.Get<double>());

    exportMatrix(prim, node, time, context);
    exportPrimvars(prim, node, time);
    exportMaterialBinding(prim, node, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");
}

// Conversion code that is common to cylinder, cone and capsule
template <class T>
void exportCylindricalShape(const UsdPrim &prim, AtNode *node, const char *radius_name)
{
    T geom(prim);

    VtValue radius_attr;
    if (geom.GetRadiusAttr().Get(&radius_attr))
        AiNodeSetFlt(node, radius_name, (float)radius_attr.Get<double>());

    float height = 1.f;
    VtValue height_attr;
    if (geom.GetHeightAttr().Get(&height_attr))
        height = (float)height_attr.Get<double>();

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

void UsdArnoldReadCylinder::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("cylinder", prim.GetPath().GetText());
    
    exportCylindricalShape<UsdGeomCylinder>(prim, node, "radius");
    const TimeSettings &time = context.getTimeSettings();
    float frame = time.frame;

    exportMatrix(prim, node, time, context);
    exportPrimvars(prim, node, time);
    exportMaterialBinding(prim, node, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");
}

void UsdArnoldReadCone::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("cone", prim.GetPath().GetText());
    
    exportCylindricalShape<UsdGeomCone>(prim, node, "bottom_radius");

    const TimeSettings &time = context.getTimeSettings();
    exportMatrix(prim, node, time, context);
    exportPrimvars(prim, node, time);
    exportMaterialBinding(prim, node, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");
}

// Note that we don't have capsule shapes in Arnold. Do we want to make a
// special case, and combine cylinders with spheres, or is it enough for now ?
void UsdArnoldReadCapsule::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("cylinder", prim.GetPath().GetText());
    
    exportCylindricalShape<UsdGeomCapsule>(prim, node, "radius");
    const TimeSettings &time = context.getTimeSettings();
    exportMatrix(prim, node, time, context);
    exportPrimvars(prim, node, time);
    exportMaterialBinding(prim, node, context);
    readArnoldParameters(prim, context, node, time, "primvars:arnold");
}

void UsdArnoldReadBounds::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("box", prim.GetPath().GetText());
    
    if (!prim.IsA<UsdGeomBoundable>())
        return;

    UsdGeomBoundable boundable(prim);
    const TimeSettings &time = context.getTimeSettings();
    float frame = time.frame;
    VtVec3fArray extent;

    UsdGeomBoundable::ComputeExtentFromPlugins(boundable,
                                         UsdTimeCode(frame),
                                         &extent);
    
    AiNodeSetVec(node, "min", extent[0][0], extent[0][1], extent[0][2]);
    AiNodeSetVec(node, "max", extent[1][0], extent[1][1], extent[1][2]);
    exportMatrix(prim, node, time, context);
}

void UsdArnoldReadGenericPolygons::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("polymesh", prim.GetPath().GetText());
    
    if (!prim.IsA<UsdGeomMesh>())
        return;

    UsdGeomMesh mesh(prim);
    const TimeSettings &time = context.getTimeSettings();
    float frame = time.frame;

    MeshOrientation mesh_orientation;
    // Get orientation. If Left-handed, we will need to invert the vertex
    // indices
    {
        TfToken orientation_token;
        if (mesh.GetOrientationAttr().Get(&orientation_token)) {
            if (orientation_token == UsdGeomTokens->leftHanded) {
                mesh_orientation.reverse = true;
                mesh.GetFaceVertexCountsAttr().Get(&mesh_orientation.nsides_array, frame);
            }
        }
    }
    exportArray<int, unsigned char>(mesh.GetFaceVertexCountsAttr(), node, "nsides", time);

    if (!mesh_orientation.reverse) {
        // Basic right-handed orientation, no need to do anything special here
        exportArray<int, unsigned int>(mesh.GetFaceVertexIndicesAttr(), node, "vidxs", time);
    } else {
        // We can't call exportArray here because the orientation requires to
        // reverse face attributes. So we're duplicating the function here.
        VtIntArray array;
        mesh.GetFaceVertexIndicesAttr().Get(&array, frame);
        size_t size = array.size();
        if (size > 0) {
            mesh_orientation.orient_face_index_attribute(array);

            // Need to convert the data from int to unsigned int
            std::vector<unsigned int> arnold_vec(array.begin(), array.end());
            AiNodeSetArray(node, "vidxs", AiArrayConvert(size, 1, AI_TYPE_UINT, arnold_vec.data()));
        } else
            AiNodeResetParameter(node, "vidxs");
    } 
    exportArray<GfVec3f, GfVec3f>(mesh.GetPointsAttr(), node, "vlist", time);
    exportMatrix(prim, node, time, context);
}

void UsdArnoldReadGenericPoints::read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AtNode *node = context.createArnoldNode("points", prim.GetPath().GetText());
    
    if (!prim.IsA<UsdGeomPointBased>())
        return;
    
    const TimeSettings &time = context.getTimeSettings();
    UsdGeomPointBased points(prim);
    exportArray<GfVec3f, GfVec3f>(points.GetPointsAttr(), node, "points", time);
}
