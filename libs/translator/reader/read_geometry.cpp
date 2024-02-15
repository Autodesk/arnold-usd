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
#if PXR_VERSION >= 2111
#include <pxr/usd/usdLux/nonboundableLightBase.h>
#include <pxr/usd/usdLux/boundableLightBase.h>
#else
#include <pxr/usd/usdLux/light.h>
#endif
#if PXR_VERSION >= 2302
#include <pxr/usd/usdLux/lightAPI.h>
#endif

#include <constant_strings.h>
#include <shape_utils.h>
#include <parameters_utils.h>

#include "utils.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (LightAPI)
    ((PrimvarsArnoldLightShaders, "primvars:arnold:light:shaders"))
);

namespace {

static inline void _ReadPointsAndVertices(AtNode *node, const VtIntArray &numVerts,
                                    const VtIntArray &verts, const VtVec3fArray &points)
{
    size_t nsize = numVerts.size();
    VtArray<unsigned char> nsides;
    nsides.assign(numVerts.cbegin(), numVerts.cend());
    AiNodeSetArray(node, str::nsides, AiArrayConvert(nsize, 1, AI_TYPE_BYTE, nsides.cdata()));

    size_t vsize = verts.size();
    VtArray<unsigned int> vidxs;
    vidxs.assign(verts.cbegin(), verts.cend());
    AiNodeSetArray(node, str::vidxs, AiArrayConvert(vsize, 1, AI_TYPE_UINT, vidxs.cdata()));

    const GfVec3f *vlist = points.cdata();
    size_t psize = points.size();
    AiNodeSetArray(node, str::vlist, AiArrayConvert(psize, 1, AI_TYPE_VECTOR, vlist));
}

/**
 * Read a UsdGeomPointsBased points attribute to get its positions, as well as its velocities
 * If velocities are found, we just get the positions at the "current" frame, and interpolate
 * to compute the positions keys.
 * If no velocities are found, we get the positions at the different motion steps
 * Return true in the first case, false otherwise
 **/
static inline bool _ReadPointsAndVelocities(const UsdGeomPointBased &geom, AtNode *node,
                                        const char *attrName, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    UsdAttribute pointsAttr = geom.GetPointsAttr();
    UsdAttribute velAttr = geom.GetVelocitiesAttr();
    
    VtValue velValue;
    if (time.motionBlur && velAttr && velAttr.Get(&velValue, time.frame)) {
        // How many samples do we want
        // Arnold support only timeframed arrays with the same number of points which can be a problem
        // The timeframe are equally spaced
        std::vector<UsdTimeCode> timeSamples;
        int numKeys = GetTimeSampleNumKeys(geom.GetPrim(), time);
        VtArray<GfVec3f> pointsTmp;
        // arnold points - that could probably be optimized, allocating only AtArray
        std::vector<GfVec3f> points;
        int numPoints = 0;
        for(int i = 0; i < numKeys; ++i) {
            pointsTmp.clear();
            double timeSample = time.frame;
            if (numKeys > 1) {
                timeSample += time.motionStart + i * (time.motionEnd - time.motionStart) / (numKeys - 1.0);
            }
            if (geom.ComputePointsAtTime(&pointsTmp, UsdTimeCode(timeSample), UsdTimeCode(time.frame))){
                numPoints = pointsTmp.size(); // We could check if the number of points are always the same, but 
                // ComputePointsAtTime is supposed to return the same number of points for each samples.

                // In the unlikely case where this geo has velocity and skinning.
                VtArray<GfVec3f> skinnedPosArray;
                UsdArnoldSkelData *skelData = context.GetSkelData();
                if (skelData && skelData->ApplyPointsSkinning(pointsAttr.GetPrim(), pointsTmp, skinnedPosArray, 
                                                context, time.frame, UsdArnoldSkelData::SKIN_POINTS)) {
                    // skinnedPosArray can be empty which can lead to the geometry not being set
                    points.insert(points.end(), skinnedPosArray.begin(), skinnedPosArray.end());
                } else {
                    points.insert(points.end(), pointsTmp.begin(), pointsTmp.end());
                }
            } else {
                TF_CODING_ERROR(
                    "%s -- unable to compute the point positions", 
                    pointsAttr.GetPrim().GetPath().GetText());
            }
        }
        // Make sure we have the right number of points before assigning them to arnold
        if (points.size() == numKeys * numPoints) {
            AiNodeSetArray(node, AtString(attrName), AiArrayConvert(numPoints, numKeys, AI_TYPE_VECTOR, points.data()));
        }
        // We need to set the motion start and motion end
        // corresponding the array keys we've just set
        AiNodeSetFlt(node, str::motion_start, time.motionStart);
        AiNodeSetFlt(node, str::motion_end, time.motionEnd);
        return true;
    }
    unsigned int keySize = ReadTopology(pointsAttr, node, attrName, time, context);
    // No velocities, let's read the positions, eventually at different motion frames
    if (keySize > 1) {
        // We got more than 1 key, so we need to set the motion start/end
        AiNodeSetFlt(node, str::motion_start, time.motionStart);
        AiNodeSetFlt(node, str::motion_end, time.motionEnd);
    }
    return false;
}

static inline void _ReadSidedness(UsdGeomGprim &geom, AtNode *node, float frame) 
{
    VtValue value;
    if (geom.GetDoubleSidedAttr().Get(&value, frame) && VtValueGetBool(value))
        AiNodeSetByte(node, str::sidedness, AI_RAY_ALL);
    else {
        // USD defaults to single sided mesh.
        AiNodeSetByte(node, str::sidedness, AI_RAY_SUBSURFACE);
    }
}

void ReadMeshLight(const UsdPrim &prim, UsdArnoldReaderContext &context, AtNode *node, const TimeSettings &time) {
    // Check if there is a parameter primvars:arnold:light
    float frame = time.frame;
    UsdAttribute meshLightAttr = prim.GetAttribute(str::t_primvars_arnold_light);
    bool meshLight = false;
    if (meshLightAttr && meshLightAttr.Get(&meshLight, frame) && meshLight) {
        // we have a geometry light for this mesh
        std::string lightName = AiNodeGetName(node);
        lightName += "/light";
        AtNode *meshLightNode = context.CreateArnoldNode("mesh_light", lightName.c_str());
        AiNodeSetPtr(meshLightNode, str::mesh, (void*)node);
        // Read the arnold parameters for this light
        ReadArnoldParameters(prim, context, meshLightNode, time, "primvars:arnold:light");
        ReadLightShaders(prim, prim.GetAttribute(_tokens->PrimvarsArnoldLightShaders), meshLightNode, context);
    }
}


} // namespace

struct MeshOrientation {
    MeshOrientation() : reverse(false) {}

    VtIntArray nsidesArray;
    bool reverse;
    template <class T>
    bool OrientFaceIndexAttribute(T& attr);
};
// Reverse an attribute of the face. Basically, it converts from the clockwise
// to the counterclockwise and back.
template <class T>
bool MeshOrientation::OrientFaceIndexAttribute(T& attr)
{
    if (!reverse)
        return true;

    size_t attrSize = attr.size();
    size_t counter = 0;
    for (auto npoints : nsidesArray) {
        for (size_t j = 0; j < (size_t) npoints / 2; j++) {
            size_t from = counter + j;
            size_t to = counter + npoints - 1 - j;
            if (from >= attrSize || to >= attrSize) 
                return false;
            std::swap(attr[from], attr[to]);
        }
        counter += npoints;
    }
    return true;
}

class MeshPrimvarsRemapper : public PrimvarsRemapper
{
public:
    MeshPrimvarsRemapper(MeshOrientation &orientation) : _orientation(orientation) {}
    virtual ~MeshPrimvarsRemapper() {}

    bool RemapIndexes(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
        std::vector<unsigned int> &indexes) override;
private:
    MeshOrientation &_orientation;
};
bool MeshPrimvarsRemapper::RemapIndexes(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
        std::vector<unsigned int> &indexes) 
{
    if (interpolation != UsdGeomTokens->faceVarying)
        return false;

    if (!_orientation.OrientFaceIndexAttribute(indexes)) {
        const UsdAttribute &attr = primvar.GetAttr();

        AiMsgWarning(
                "[usd] Invalid primvar indices in %s.%s", attr.GetPrim().GetPath().GetString().c_str(),
                attr.GetName().GetString().c_str());

    }
    return true;
}
/** Reading a USD Mesh description to Arnold
 **/
AtNode* UsdArnoldReadMesh::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    // For some attributes, we should never try to read them with motion blur,
    // we use another timeSettings for them
    TimeSettings staticTime(time);
    staticTime.motionBlur = false;

    AtNode *node = context.CreateArnoldNode("polymesh", prim.GetPath().GetText());

    AiNodeSetBool(node, str::smoothing, true);

    // Get mesh.
    UsdGeomMesh mesh(prim);

    UsdArnoldSkelData *skelData = context.GetSkelData();
    if (skelData) {
        std::string primName = context.GetArnoldNodeName(prim.GetPath().GetText());
        skelData->CreateAdapters(context, primName);
    }

    MeshOrientation meshOrientation;
    // Get orientation. If Left-handed, we will need to invert the vertex
    // indices
    {
        TfToken orientationToken;
        if (mesh.GetOrientationAttr().Get(&orientationToken, frame)) {
            if (orientationToken == UsdGeomTokens->leftHanded) {
                meshOrientation.reverse = true;
                mesh.GetFaceVertexCountsAttr().Get(&meshOrientation.nsidesArray, frame);
            }
        }
    }
    
    ReadAttribute(mesh.GetFaceVertexCountsAttr(), node, "nsides", staticTime,
        context, AI_TYPE_ARRAY, AI_TYPE_BYTE);

    if (!meshOrientation.reverse) {
        // Basic right-handed orientation, no need to do anything special here
        ReadAttribute(mesh.GetFaceVertexIndicesAttr(), node, "vidxs", staticTime,
            context, AI_TYPE_ARRAY, AI_TYPE_UINT);
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
            AiNodeSetArray(node, str::vidxs, AiArrayConvert(size, 1, AI_TYPE_UINT, arnold_vec.data()));
        } else
            AiNodeResetParameter(node, str::vidxs);
    }

    bool hasVelocities = _ReadPointsAndVelocities(mesh, node, str::vlist, context);

    // Read USD builtin normals

    UsdAttribute normalsAttr = GetNormalsAttribute(mesh);
    if (normalsAttr.HasAuthoredValue()) {
        // normals need to have the same number of keys than vlist
        AtArray *vlistArray = AiNodeGetArray(node, str::vlist);
        const unsigned int vListKeys = (vlistArray) ? AiArrayGetNumKeys(vlistArray) : 1;
        // If velocities were authored, then we just want to check the values from the current frame
        GfInterval timeInterval = (vListKeys > 1 && !hasVelocities) ? 
                                GfInterval(time.start(), time.end()) :
                                GfInterval(frame, frame);

        std::vector<GfVec3f> normalsArray;
        std::vector<unsigned int> nidxs; // Flattened array that we are going to pass to arnold
        normalsArray.reserve(vListKeys*AiArrayGetNumElements(vlistArray));
        unsigned int normalsElemCount = -1;

        UsdGeomPrimvar normalsPrimvar(normalsAttr);
        TfToken normalsInterp = GetNormalsInterpolation(mesh);

        // We sample the normals at the same keys as the points
        for (unsigned int key = 0; key < vListKeys; ++key) {
            double timeSample = timeInterval.GetMin() +
                               ((double) key / (double)AiMax(1u, (vListKeys - 1))) * 
                               (timeInterval.GetMax() - timeInterval.GetMin());

            VtValue normalsValue;
            if (normalsAttr.Get(&normalsValue, timeSample)) {
                const VtArray<GfVec3f> &normalsVec = normalsValue.Get<VtArray<GfVec3f>>();
                VtArray<GfVec3f> skinnedArray;
                const VtArray<GfVec3f> *outNormals = &normalsVec;
                if (skelData && skelData->ApplyPointsSkinning(prim, normalsVec, skinnedArray, context, timeSample, UsdArnoldSkelData::SKIN_NORMALS)) {
                    outNormals = &skinnedArray;
                }

                if (key == 0)
                    normalsElemCount = outNormals->size();
                else if (outNormals->size() != normalsElemCount){
                    normalsArray.insert(normalsArray.end(), normalsArray.begin(), normalsArray.begin() + normalsElemCount);
                    continue;
                }
                normalsArray.insert(normalsArray.end(), outNormals->begin(), outNormals->end());
            }
        }
        if (normalsArray.empty())
            AiNodeResetParameter(node, str::nlist);
        else {
            AiNodeSetArray(node, str::nlist, AiArrayConvert(normalsElemCount, vListKeys, AI_TYPE_VECTOR, normalsArray.data()));
            TfToken normalsInterp = GetNormalsInterpolation(mesh);
            // Arnold expects indexed normals, so we need to create the nidxs list accordingly
            if (normalsInterp == UsdGeomTokens->varying || (normalsInterp == UsdGeomTokens->vertex)) {
                if (normalsPrimvar && normalsPrimvar.IsIndexed()) {
                    VtIntArray normalsIndices;
                    normalsPrimvar.GetIndices(&normalsIndices, UsdTimeCode(timeInterval.GetMin())); // same timesample as normalsElemCount - is it correct ?
                    AtArray *vidxsArray = AiNodeGetArray(node, str::vidxs);   
                    const uint32_t nbIdx = AiArrayGetNumElements(vidxsArray);
                    for (uint32_t i=0; i < nbIdx; ++i) {
                        nidxs.push_back(normalsIndices[AiArrayGetUInt(vidxsArray, i)]);
                    }
                    AiNodeSetArray(node, str::nidxs, AiArrayConvert(nidxs.size(), 1, AI_TYPE_UINT, nidxs.data()));
                } else {
                    AiNodeSetArray(node, str::nidxs, AiArrayCopy(AiNodeGetArray(node, str::vidxs)));
                }
            }
            else if (normalsInterp == UsdGeomTokens->faceVarying) 
            {
                std::vector<unsigned int> nidxs;
                if (normalsPrimvar && normalsPrimvar.IsIndexed()) {
                    VtIntArray indices;
                    normalsPrimvar.GetIndices(&indices, UsdTimeCode(frame)); 
                    nidxs.reserve(indices.size());
                    for (int ind: indices) {
                        nidxs.push_back(ind);
                    }
                }
                if (nidxs.empty()) {
                    nidxs.resize(normalsElemCount);
                    // Fill it with 0, 1, ..., 99.
                    std::iota(std::begin(nidxs), std::end(nidxs), 0);
                }
                AiNodeSetArray(node, str::nidxs, AiArrayConvert(nidxs.size(), 1, AI_TYPE_UINT, nidxs.data()));
            }
        }
    }

    _ReadSidedness(mesh, node, frame);

    // reset subdiv_iterations to 0, it might be set in readArnoldParameter
    AiNodeSetByte(node, str::subdiv_iterations, 0);
    ReadMatrix(prim, node, time, context);

    MeshPrimvarsRemapper primvarsRemapper(meshOrientation);
    ReadPrimvars(prim, node, time, context, &primvarsRemapper);

    std::vector<UsdGeomSubset> subsets = UsdGeomSubset::GetAllGeomSubsets(mesh);

    if (!subsets.empty()) {
        // Currently, subsets are only used for shader & disp_map assignments
        VtIntArray faceVtxArray;
        mesh.GetFaceVertexCountsAttr().Get(&faceVtxArray, frame);
        ReadSubsetsMaterialBinding(prim, node, context, subsets, faceVtxArray.size());
    } else {
        ReadMaterialBinding(prim, node, context);
    }

    UsdAttribute cornerWeightsAttr = mesh.GetCornerSharpnessesAttr();
    UsdAttribute creaseWeightsAttr = mesh.GetCreaseSharpnessesAttr();
    if (cornerWeightsAttr.HasAuthoredValue() || creaseWeightsAttr.HasAuthoredValue()) {
        VtIntArray cornerIndices;
        mesh.GetCornerIndicesAttr().Get(&cornerIndices, frame);
        VtArray<float> cornerWeights;
        cornerWeightsAttr.Get(&cornerWeights, frame);

        VtIntArray creaseIndices;
        mesh.GetCreaseIndicesAttr().Get(&creaseIndices, frame);
        VtArray<float> creaseWeights;
        creaseWeightsAttr.Get(&creaseWeights, frame);
        VtIntArray creaseLengths;
        mesh.GetCreaseLengthsAttr().Get(&creaseLengths, frame);
        ArnoldUsdReadCreases(node, cornerIndices, cornerWeights, creaseIndices, creaseLengths, creaseWeights);
    }

    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check if subdiv_iterations were set in ReadArnoldParameters,
    // and only set the subdiv_type if it's > 0. If we don't do this,
    // we get smoothed normals by default.
    // Also, we only read the builting subdivisionScheme if the arnold
    // attribute wasn't explcitely set above, through primvars:arnold (see #679)
    if ((!HasAuthoredAttribute(prim, str::t_primvars_arnold_subdiv_type)) &&
            (AiNodeGetByte(node, str::subdiv_iterations) > 0)) {
        TfToken subdiv;
        mesh.GetSubdivisionSchemeAttr().Get(&subdiv, time.frame);
        if (subdiv == UsdGeomTokens->none)
            AiNodeSetStr(node, str::subdiv_type, str::none);
        else if (subdiv == UsdGeomTokens->catmullClark)
            AiNodeSetStr(node, str::subdiv_type, str::catclark);
        else if (subdiv == UsdGeomTokens->bilinear)
            AiNodeSetStr(node, str::subdiv_type, str::linear);
        else
            AiMsgWarning(
                "[usd] %s subdivision scheme not supported for mesh on path %s", subdiv.GetString().c_str(),
                mesh.GetPath().GetString().c_str());
    }

    // Check the prim visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);

    ReadMeshLight(prim, context, node, time);

    return node;
}

class CurvesPrimvarsRemapper : public PrimvarsRemapper
{
public:
    CurvesPrimvarsRemapper(bool remapValues, bool pinnedCurve, ArnoldUsdCurvesData &curvesData) : 
                    _remapValues(remapValues), _pinnedCurve(pinnedCurve), _curvesData(curvesData) {}
    virtual ~CurvesPrimvarsRemapper() {}
    bool RemapValues(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
        VtValue &value) override;
    void RemapPrimvar(TfToken &name, std::string &interpolation) override;
private:
    bool _remapValues, _pinnedCurve;
    ArnoldUsdCurvesData &_curvesData;
};
bool CurvesPrimvarsRemapper::RemapValues(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
    VtValue &value)
{
    if (!_remapValues)
        return false;

    if (interpolation != UsdGeomTokens->vertex && interpolation != UsdGeomTokens->varying) 
        return false;

    if (_pinnedCurve && interpolation == UsdGeomTokens->vertex)
        return false;

    // Try to read any of the following types, depending on which type the value is holding
    return _curvesData.RemapCurvesVertexPrimvar<float, double, GfVec2f, GfVec2d, GfVec3f, 
                GfVec3d, GfVec4f, GfVec4d, int, unsigned int, unsigned char, bool>(value);

}
void CurvesPrimvarsRemapper::RemapPrimvar(TfToken &name, std::string &interpolation)
{
    // primvars:st should be converted to curves "uvs" #957
    if (name == str::t_st)
        name = str::t_uvs;
}

AtNode* UsdArnoldReadCurves::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    // For some attributes, we should never try to read them with motion blur,
    // we use another timeSettings for them
    TimeSettings staticTime(time);
    staticTime.motionBlur = false;

    UsdGeomCurves curves(prim);
   
    AtNode *node = context.CreateArnoldNode("curves", prim.GetPath().GetText());

    AtString basis = str::linear;
    bool isValidPinnedCurve = false;
    if (prim.IsA<UsdGeomBasisCurves>()) {
        // TODO: use a scope_pointer for curves and basisCurves.
        UsdGeomBasisCurves basisCurves(prim);
        TfToken curveType, wrapMode;
        basisCurves.GetTypeAttr().Get(&curveType, frame);
        basisCurves.GetWrapAttr().Get(&wrapMode, frame);
        if (curveType == UsdGeomTokens->cubic) {
            TfToken basisType;
            basisCurves.GetBasisAttr().Get(&basisType, frame);
            if (basisType == UsdGeomTokens->bezier)
                basis = str::bezier;
            else if (basisType == UsdGeomTokens->bspline)
                basis = str::b_spline;
            else if (basisType == UsdGeomTokens->catmullRom)
                basis = str::catmull_rom;
#if ARNOLD_VERSION_NUM >= 70103
            if (basisType == UsdGeomTokens->bspline || basisType == UsdGeomTokens->catmullRom) {
                AiNodeSetStr(node, str::wrap_mode, AtString(wrapMode.GetText()));
                if (wrapMode == UsdGeomTokens->pinned)
                    isValidPinnedCurve = true;
            }
#endif
        }
    }

    AiNodeSetStr(node, str::basis, basis);

    // CV counts per curve
    ReadAttribute(curves.GetCurveVertexCountsAttr(), node, "num_points", staticTime,
            context, AI_TYPE_ARRAY, AI_TYPE_UINT);
    
    // CVs positions
    _ReadPointsAndVelocities(curves, node, "points", context);

    // Widths
    // We need to divide the width by 2 in order to get the radius for arnold points
    VtIntArray vertexCounts;
    curves.GetCurveVertexCountsAttr().Get(&vertexCounts, frame);
    const auto vstep = basis == str::bezier ? 3 : 1;
    const auto vmin = basis == str::linear ? 2 : 4;
    ArnoldUsdCurvesData curvesData(vmin, vstep, vertexCounts);

    VtValue widthValues;
    if (curves.GetWidthsAttr().Get(&widthValues, frame)) {
        TfToken widthInterpolation = curves.GetWidthsInterpolation();
        if ((widthInterpolation == UsdGeomTokens->vertex || widthInterpolation == UsdGeomTokens->varying) &&
                basis != str::linear) {
            // if radius data is per-vertex and the curve is pinned, then don't remap
            if (!(widthInterpolation == UsdGeomTokens->vertex && isValidPinnedCurve))
                curvesData.RemapCurvesVertexPrimvar<float, double>(widthValues);
            curvesData.SetRadiusFromValue(node, widthValues);
        } else {
            curvesData.SetRadiusFromValue(node, widthValues);
        }
    } else {
        // Width isn't defined, we assume a constant width equal to 1
        AiNodeSetFlt(node, str::radius, 0.5);
    }
    VtValue normalsValues;
    if (curves.GetNormalsAttr().Get(&normalsValues, frame)) {
        if (basis == str::linear)
            AiMsgWarning("%s : Orientations not supported on linear curves", AiNodeGetName(node));
        else
            curvesData.SetOrientationFromValue(node, normalsValues);
    }

    ReadMatrix(prim, node, time, context);
    CurvesPrimvarsRemapper primvarsRemapper((basis != str::linear), isValidPinnedCurve, curvesData);

    ReadPrimvars(prim, node, time, context, &primvarsRemapper);
    std::vector<UsdGeomSubset> subsets = UsdGeomSubset::GetAllGeomSubsets(curves);

    if (!subsets.empty()) {
        // Currently, subsets are only used for shader & disp_map assignments
        VtIntArray curveVtxArray;
        curves.GetCurveVertexCountsAttr().Get(&curveVtxArray, frame);
        ReadSubsetsMaterialBinding(prim, node, context, subsets, curveVtxArray.size());
    } else {
        ReadMaterialBinding(prim, node, context);
    }

    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the prim visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);
    return node;
}

AtNode* UsdArnoldReadPoints::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    AtNode *node = context.CreateArnoldNode("points", prim.GetPath().GetText());

    UsdGeomPoints points(prim);

    // Points positions
    _ReadPointsAndVelocities(points, node, "points", context);

    AtArray *pointsArray = AiNodeGetArray(node, AtString("points"));
    unsigned int pointsSize = (pointsArray) ? AiArrayGetNumElements(pointsArray) : 0;

    // Points radius
    // We need to divide the width by 2 in order to get the radius for arnold points
    VtArray<float> widthArray;
    if (points.GetWidthsAttr().Get(&widthArray, frame)) {
        size_t widthCount = widthArray.size();
        if (widthCount <= 1 && pointsSize > widthCount) {
            // USD accepts empty width attributes, or a constant width for all points,
            // but arnold fails in that case. So we need to generate a dedicated array
            float radiusVal = (widthCount == 0) ? 0.f : widthArray[0] * 0.5f;
            // Create an array where each point has the same radius
            std::vector<float> radiusVec(pointsSize, radiusVal);
            AiNodeSetArray(node, str::radius, AiArrayConvert(pointsSize, 1, AI_TYPE_FLOAT, &radiusVec[0]));
        } else if (widthCount > 0) {
            AtArray *radiusArray = AiArrayAllocate(widthCount, 1, AI_TYPE_FLOAT);
            float *out = static_cast<float *>(AiArrayMap(radiusArray));
            for (unsigned int i = 0; i < widthCount; ++i) {
                out[i] = widthArray[i] * 0.5f;
            }
            AiArrayUnmap(radiusArray);
            AiNodeSetArray(node, str::radius, radiusArray);
        }
    }

    ReadMatrix(prim, node, time, context);

    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");
    // Check the primitive visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);

    return node;
}

/**
 *   Convert the basic USD shapes (cube, sphere, cylinder, cone,...)
 *   to Arnold. There are 2 main differences so far :
 *      - capsules don't exist in arnold
 *      - cylinders are different (one is closed the other isn't)
 **/
AtNode* UsdArnoldReadCube::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;
    AtNode *node = context.CreateArnoldNode("polymesh", prim.GetPath().GetText());
    AiNodeSetBool(node, str::smoothing, false);
    
    static const VtIntArray numVerts { 4, 4, 4, 4, 4, 4 };
    static const VtIntArray verts { 0, 1, 2, 3,
                                    4, 5, 6, 7,
                                    0, 6, 5, 1,
                                    4, 7, 3, 2,
                                    0, 3, 7, 6,
                                    4, 2, 1, 5 };
    VtVec3fArray points {   GfVec3f( 0.5f,  0.5f,  0.5f),
                            GfVec3f(-0.5f,  0.5f,  0.5f),
                            GfVec3f(-0.5f, -0.5f,  0.5f),
                            GfVec3f( 0.5f, -0.5f,  0.5f),
                            GfVec3f(-0.5f, -0.5f, -0.5f),
                            GfVec3f(-0.5f,  0.5f, -0.5f),
                            GfVec3f( 0.5f,  0.5f, -0.5f),
                            GfVec3f( 0.5f, -0.5f, -0.5f) };

    UsdGeomCube cube(prim);

    VtValue sizeValue;
    if (!cube.GetSizeAttr().Get(&sizeValue, frame))
        AiMsgWarning("Could not evaluate size attribute on prim %s",
            prim.GetPath().GetText());
    float size = VtValueGetFloat(sizeValue);

    GfMatrix4d scale(   size,  0.0,  0.0, 0.0,
                        0.0, size,  0.0, 0.0,
                        0.0,  0.0, size, 0.0,
                        0.0,  0.0,  0.0, 1.0);
    for (GfVec3f& pt : points)
        pt = scale.Transform(pt);

    _ReadSidedness(cube, node, frame);
    _ReadPointsAndVertices(node, numVerts, verts, points);
    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    ReadMeshLight(prim, context, node, time);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);
    return node;
}

AtNode* UsdArnoldReadSphere::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;
    AtNode *node = context.CreateArnoldNode("polymesh", prim.GetPath().GetText());
    AiNodeSetBool(node, str::smoothing, true);
    
    static const VtIntArray numVerts{
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3};

    static const VtIntArray verts{
        // Tris
        2, 1, 0,  3, 2, 0,  4, 3, 0,  5, 4, 0,  6, 5, 0, 
        7, 6, 0,  8, 7, 0,  9, 8, 0,  10, 9, 0, 1, 10, 0, 
        // Quads
        1, 2, 12, 11, 2, 3, 13, 12, 3, 4, 14, 13, 4, 5, 15, 14,
        5, 6, 16, 15, 6, 7, 17, 16, 7, 8, 18, 17, 8, 9, 19, 18, 
        9, 10, 20, 19, 10, 1, 11, 20, 11, 12, 22, 21, 12, 13, 23, 22,
        13, 14, 24, 23, 14, 15, 25, 24, 15, 16, 26, 25, 16, 17, 27, 26,
        17, 18, 28, 27, 18, 19, 29, 28, 19, 20, 30, 29, 20, 11, 21, 30,
        21, 22, 32, 31, 22, 23, 33, 32, 23, 24, 34, 33, 24, 25, 35, 34,
        25, 26, 36, 35, 26, 27, 37, 36, 27, 28, 38, 37, 28, 29, 39, 38,
        29, 30, 40, 39, 30, 21, 31, 40, 31, 32, 42, 41, 32, 33, 43, 42,
        33, 34, 44, 43, 34, 35, 45, 44, 35, 36, 46, 45, 36, 37, 47, 46,
        37, 38, 48, 47, 38, 39, 49, 48, 39, 40, 50, 49, 40, 31, 41, 50,
        41, 42, 52, 51, 42, 43, 53, 52, 43, 44, 54, 53, 44, 45, 55, 54,
        45, 46, 56, 55, 46, 47, 57, 56, 47, 48, 58, 57, 48, 49, 59, 58,
        49, 50, 60, 59, 50, 41, 51, 60, 51, 52, 62, 61, 52, 53, 63, 62,
        53, 54, 64, 63, 54, 55, 65, 64, 55, 56, 66, 65, 56, 57, 67, 66,
        57, 58, 68, 67, 58, 59, 69, 68, 59, 60, 70, 69, 60, 51, 61, 70,
        61, 62, 72, 71, 62, 63, 73, 72, 63, 64, 74, 73, 64, 65, 75, 74,
        65, 66, 76, 75, 66, 67, 77, 76, 67, 68, 78, 77, 68, 69, 79, 78,
        69, 70, 80, 79, 70, 61, 71, 80, 71, 72, 82, 81, 72, 73, 83, 82,
        73, 74, 84, 83, 74, 75, 85, 84, 75, 76, 86, 85, 76, 77, 87, 86,
        77, 78, 88, 87, 78, 79, 89, 88, 79, 80, 90, 89, 80, 71, 81, 90,
        // Tris
        81, 82, 91,  82, 83, 91,  83, 84, 91,   84, 85, 91,   85, 86, 91,
        86, 87, 91,  87, 88, 91,  88, 89, 91,   89, 90, 91,   90, 81, 91};

    VtVec3fArray points{
        GfVec3f(0, 0, -1),                            GfVec3f(0.30901697, 0, -0.95105654), 
        GfVec3f(0.24999999, 0.18163562, -0.95105654), GfVec3f(0.09549149, 0.29389262, -0.95105654), 
        GfVec3f(-0.09549154, 0.2938926, -0.95105654), GfVec3f(-0.25, 0.1816356, -0.95105654), 
        GfVec3f(-0.30901697, -2.7015123e-8, -0.95105654), GfVec3f(-0.24999991, -0.18163571, -0.95105654),
        GfVec3f(-0.09549153, -0.2938926, -0.95105654), GfVec3f(0.095491536, -0.2938926, -0.95105654), 
        GfVec3f(0.24999997, -0.18163563, -0.95105654), GfVec3f(0.5877853, 0, -0.809017), 
        GfVec3f(0.4755283, 0.34549153, -0.809017), GfVec3f(0.18163563, 0.55901706, -0.809017),
        GfVec3f(-0.18163574, 0.559017, -0.809017), GfVec3f(-0.47552833, 0.3454915, -0.809017),
        GfVec3f(-0.5877853, -5.1385822e-8, -0.809017), GfVec3f(-0.47552815, -0.3454917, -0.809017),
        GfVec3f(-0.18163571, -0.559017, -0.809017), GfVec3f(0.18163572, -0.559017, -0.809017),
        GfVec3f(0.47552827, -0.34549156, -0.809017), GfVec3f(0.809017, 0, -0.58778524),
        GfVec3f(0.65450853, 0.47552827, -0.58778524), GfVec3f(0.24999999, 0.7694209, -0.58778524),
        GfVec3f(-0.25000012, 0.76942086, -0.58778524), GfVec3f(-0.65450853, 0.4755282, -0.58778524),
        GfVec3f(-0.809017, -7.0726514e-8, -0.58778524), GfVec3f(-0.6545083, -0.4755285, -0.58778524),
        GfVec3f(-0.2500001, -0.76942086, -0.58778524), GfVec3f(0.25000012, -0.76942086, -0.58778524),
        GfVec3f(0.6545085, -0.4755283, -0.58778524), GfVec3f(0.95105654, 0, -0.30901697), 
        GfVec3f(0.7694209, 0.559017, -0.30901697), GfVec3f(0.29389262, 0.90450853, -0.30901697),
        GfVec3f(-0.29389277, 0.9045085, -0.30901697), GfVec3f(-0.769421, 0.55901694, -0.30901697),
        GfVec3f(-0.95105654, -8.3144e-8, -0.30901697), GfVec3f(-0.7694207, -0.5590173, -0.30901697),
        GfVec3f(-0.29389274, -0.9045085, -0.30901697), GfVec3f(0.29389274, -0.9045085, -0.30901697),
        GfVec3f(0.76942086, -0.55901706, -0.30901697), GfVec3f(1, 0, 0),
        GfVec3f(0.809017, 0.58778524, 0), GfVec3f(0.30901697, 0.95105654, 0), 
        GfVec3f(-0.30901715, 0.9510565, 0), GfVec3f(-0.80901706, 0.5877852, 0),
        GfVec3f(-1, -8.742278e-8, 0), GfVec3f(-0.80901676, -0.58778554, 0),
        GfVec3f(-0.3090171, -0.9510565, 0), GfVec3f(0.30901712, -0.9510565, 0),
        GfVec3f(0.80901694, -0.5877853, 0), GfVec3f(0.9510565, 0, 0.30901706),
        GfVec3f(0.76942086, 0.55901694, 0.30901706), GfVec3f(0.2938926, 0.9045085, 0.30901706),
        GfVec3f(-0.29389277, 0.9045084, 0.30901706), GfVec3f(-0.7694209, 0.5590169, 0.30901706),
        GfVec3f(-0.9510565, -8.3143995e-8, 0.30901706), GfVec3f(-0.7694206, -0.55901724, 0.30901706),
        GfVec3f(-0.2938927, -0.9045084, 0.30901706), GfVec3f(0.29389274, -0.9045084, 0.30901706),
        GfVec3f(0.7694208, -0.559017, 0.30901706), GfVec3f(0.809017, 0, 0.58778524), 
        GfVec3f(0.65450853, 0.47552827, 0.58778524), GfVec3f(0.24999999, 0.7694209, 0.58778524),
        GfVec3f(-0.25000012, 0.76942086, 0.58778524), GfVec3f(-0.65450853, 0.4755282, 0.58778524),
        GfVec3f(-0.809017, -7.0726514e-8, 0.58778524), GfVec3f(-0.6545083, -0.4755285, 0.58778524),
        GfVec3f(-0.2500001, -0.76942086, 0.58778524), GfVec3f(0.25000012, -0.76942086, 0.58778524),
        GfVec3f(0.6545085, -0.4755283, 0.58778524), GfVec3f(0.58778524, 0, 0.809017),
        GfVec3f(0.47552827, 0.3454915, 0.809017), GfVec3f(0.18163562, 0.559017, 0.809017),
        GfVec3f(-0.18163572, 0.55901694, 0.809017), GfVec3f(-0.4755283, 0.34549147, 0.809017),
        GfVec3f(-0.58778524, -5.138582e-8, 0.809017), GfVec3f(-0.47552812, -0.34549168, 0.809017),
        GfVec3f(-0.1816357, -0.55901694, 0.809017), GfVec3f(0.18163571, -0.55901694, 0.809017),
        GfVec3f(0.4755282, -0.34549153, 0.809017), GfVec3f(0.30901706, 0, 0.9510565),
        GfVec3f(0.25000006, 0.18163566, 0.9510565), GfVec3f(0.09549151, 0.2938927, 0.9510565),
        GfVec3f(-0.09549157, 0.29389268, 0.9510565), GfVec3f(-0.2500001, 0.18163565, 0.9510565),
        GfVec3f(-0.30901706, -2.701513e-8, 0.9510565), GfVec3f(-0.24999999, -0.18163577, 0.9510565),
        GfVec3f(-0.09549155, -0.29389268, 0.9510565), GfVec3f(0.095491566, -0.29389268, 0.9510565),
        GfVec3f(0.25000003, -0.1816357, 0.9510565), GfVec3f(0, 0, 1)};

    // Get implicit geom scale transform
    UsdGeomSphere sphere(prim);

    VtValue radiusValue;
    if (sphere.GetRadiusAttr().Get(&radiusValue, frame)) {
        double radius = VtValueGetFloat(radiusValue);
        if (std::abs(radius - 1.f) > AI_EPSILON) {
            GfMatrix4d scale(radius,  0.0,  0.0, 0.0,
                            0.0, radius,  0.0, 0.0,
                            0.0,  0.0, radius, 0.0,
                            0.0,  0.0,  0.0, 1.0);
            for (GfVec3f& pt : points)
                pt = scale.Transform(pt);
        }
    }

    _ReadSidedness(sphere, node, frame);
    _ReadPointsAndVertices(node, numVerts, verts, points);
    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    ReadMeshLight(prim, context, node, time);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);
    return node;
}

// Conversion code that is common to cylinder, cone and capsule
template <class T>
GfMatrix4d exportCylindricalTransform(const UsdPrim &prim, AtNode *node, float frame)
{
    T geom(prim);

    VtValue radiusValue;
    if (!geom.GetRadiusAttr().Get(&radiusValue, frame))
        AiMsgWarning("Could not evaluate radius attribute on prim %s",
            prim.GetPath().GetText());
    float radius = VtValueGetFloat(radiusValue);

    VtValue heightValue;
    if (!geom.GetHeightAttr().Get(&heightValue, frame))
        AiMsgWarning("Could not evaluate height attribute on prim %s",
            prim.GetPath().GetText());
    float height = VtValueGetFloat(heightValue);

    TfToken axis = UsdGeomTokens->z;
    if (!geom.GetAxisAttr().Get(&axis, frame))
        AiMsgWarning("Could not evaluate axis attribute on prim %s",
            prim.GetPath().GetText());

    const double diameter = 2.0 * radius;
    GfMatrix4d scale;
    if (axis == UsdGeomTokens->x) {
        scale.Set(     0.0, diameter,      0.0, 0.0,
                               0.0,      0.0, diameter, 0.0,
                            height,      0.0,      0.0, 0.0,
                               0.0,      0.0,      0.0, 1.0);
    }
    else if (axis == UsdGeomTokens->y) {
        scale.Set(     0.0,      0.0, diameter, 0.0,
                          diameter,      0.0,      0.0, 0.0,
                               0.0,   height,      0.0, 0.0,
                               0.0,      0.0,      0.0, 1.0);
    }
    else { // (axis == UsdGeomTokens->z)
        scale.Set(diameter,      0.0,      0.0, 0.0,
                               0.0, diameter,      0.0, 0.0,
                               0.0,      0.0,   height, 0.0,
                               0.0,      0.0,      0.0, 1.0);
    }

    return scale;
}

AtNode* UsdArnoldReadCylinder::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;
    AtNode *node = context.CreateArnoldNode("polymesh", prim.GetPath().GetText());
    AiNodeSetBool(node, str::smoothing, true);
    static const VtIntArray numVerts{ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                      4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                                      3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
    static const VtIntArray verts{
        // Tris
         2,  1,  0,    3,  2,  0,    4,  3,  0,    5,  4,  0,    6,  5,  0,
         7,  6,  0,    8,  7,  0,    9,  8,  0,   10,  9,  0,    1, 10,  0,
        // Quads
        11, 12, 22, 21,   12, 13, 23, 22,   13, 14, 24, 23,   14, 15, 25, 24,
        15, 16, 26, 25,   16, 17, 27, 26,   17, 18, 28, 27,   18, 19, 29, 28,
        19, 20, 30, 29,   20, 11, 21, 30,
        // Tris
        31, 32, 41,   32, 33, 41,   33, 34, 41,   34, 35, 41,   35, 36, 41,
        36, 37, 41,   37, 38, 41,   38, 39, 41,   39, 40, 41,   40, 31, 41 };

    VtVec3fArray points{
        GfVec3f( 0.0000,  0.0000, -0.5000), GfVec3f( 0.5000,  0.0000, -0.5000),
        GfVec3f( 0.4045,  0.2939, -0.5000), GfVec3f( 0.1545,  0.4755, -0.5000),
        GfVec3f(-0.1545,  0.4755, -0.5000), GfVec3f(-0.4045,  0.2939, -0.5000),
        GfVec3f(-0.5000,  0.0000, -0.5000), GfVec3f(-0.4045, -0.2939, -0.5000),
        GfVec3f(-0.1545, -0.4755, -0.5000), GfVec3f( 0.1545, -0.4755, -0.5000),
        GfVec3f( 0.4045, -0.2939, -0.5000), GfVec3f( 0.5000,  0.0000, -0.5000),
        GfVec3f( 0.4045,  0.2939, -0.5000), GfVec3f( 0.1545,  0.4755, -0.5000),
        GfVec3f(-0.1545,  0.4755, -0.5000), GfVec3f(-0.4045,  0.2939, -0.5000),
        GfVec3f(-0.5000,  0.0000, -0.5000), GfVec3f(-0.4045, -0.2939, -0.5000),
        GfVec3f(-0.1545, -0.4755, -0.5000), GfVec3f( 0.1545, -0.4755, -0.5000),
        GfVec3f( 0.4045, -0.2939, -0.5000), GfVec3f( 0.5000,  0.0000,  0.5000),
        GfVec3f( 0.4045,  0.2939,  0.5000), GfVec3f( 0.1545,  0.4755,  0.5000),
        GfVec3f(-0.1545,  0.4755,  0.5000), GfVec3f(-0.4045,  0.2939,  0.5000),
        GfVec3f(-0.5000,  0.0000,  0.5000), GfVec3f(-0.4045, -0.2939,  0.5000),
        GfVec3f(-0.1545, -0.4755,  0.5000), GfVec3f( 0.1545, -0.4755,  0.5000),
        GfVec3f( 0.4045, -0.2939,  0.5000), GfVec3f( 0.5000,  0.0000,  0.5000),
        GfVec3f( 0.4045,  0.2939,  0.5000), GfVec3f( 0.1545,  0.4755,  0.5000),
        GfVec3f(-0.1545,  0.4755,  0.5000), GfVec3f(-0.4045,  0.2939,  0.5000),
        GfVec3f(-0.5000,  0.0000,  0.5000), GfVec3f(-0.4045, -0.2939,  0.5000),
        GfVec3f(-0.1545, -0.4755,  0.5000), GfVec3f( 0.1545, -0.4755,  0.5000),
        GfVec3f( 0.4045, -0.2939,  0.5000), GfVec3f( 0.0000,  0.0000,  0.5000)};

    // Get implicit geom scale transform
    GfMatrix4d scale = exportCylindricalTransform<UsdGeomCylinder>(prim, node, frame);
    for (GfVec3f& pt : points)
        pt = scale.Transform(pt);

    UsdGeomCylinder cylinder(prim);

    _ReadSidedness(cylinder, node, frame);
    _ReadPointsAndVertices(node, numVerts, verts, points);
    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    ReadMeshLight(prim, context, node, time);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);
    return node;
}

AtNode* UsdArnoldReadCone::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;
    AtNode *node = context.CreateArnoldNode("polymesh", prim.GetPath().GetText());
    AiNodeSetBool(node, str::smoothing, true);
    
    static const VtIntArray numVerts{ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                      4, 4, 4, 4, 4, 4, 4, 4, 4, 4 };
    static const VtIntArray verts{
        // Tris
         2,  1,  0,    3,  2,  0,    4,  3,  0,    5,  4,  0,    6,  5,  0,
         7,  6,  0,    8,  7,  0,    9,  8,  0,   10,  9,  0,    1, 10,  0,
        // Quads
        11, 12, 22, 21,   12, 13, 23, 22,   13, 14, 24, 23,   14, 15, 25, 24,
        15, 16, 26, 25,   16, 17, 27, 26,   17, 18, 28, 27,   18, 19, 29, 28,
        19, 20, 30, 29,   20, 11, 21, 30 };

    VtVec3fArray points{
        GfVec3f( 0.0000,  0.0000, -0.5000), GfVec3f( 0.5000,  0.0000, -0.5000),
        GfVec3f( 0.4045,  0.2939, -0.5000), GfVec3f( 0.1545,  0.4755, -0.5000),
        GfVec3f(-0.1545,  0.4755, -0.5000), GfVec3f(-0.4045,  0.2939, -0.5000),
        GfVec3f(-0.5000,  0.0000, -0.5000), GfVec3f(-0.4045, -0.2939, -0.5000),
        GfVec3f(-0.1545, -0.4755, -0.5000), GfVec3f( 0.1545, -0.4755, -0.5000),
        GfVec3f( 0.4045, -0.2939, -0.5000), GfVec3f( 0.5000,  0.0000, -0.5000),
        GfVec3f( 0.4045,  0.2939, -0.5000), GfVec3f( 0.1545,  0.4755, -0.5000),
        GfVec3f(-0.1545,  0.4755, -0.5000), GfVec3f(-0.4045,  0.2939, -0.5000),
        GfVec3f(-0.5000,  0.0000, -0.5000), GfVec3f(-0.4045, -0.2939, -0.5000),
        GfVec3f(-0.1545, -0.4755, -0.5000), GfVec3f( 0.1545, -0.4755, -0.5000),
        GfVec3f( 0.4045, -0.2939, -0.5000), GfVec3f( 0.0000,  0.0000,  0.5000),
        GfVec3f( 0.0000,  0.0000,  0.5000), GfVec3f( 0.0000,  0.0000,  0.5000),
        GfVec3f( 0.0000,  0.0000,  0.5000), GfVec3f( 0.0000,  0.0000,  0.5000),
        GfVec3f( 0.0000,  0.0000,  0.5000), GfVec3f( 0.0000,  0.0000,  0.5000),
        GfVec3f( 0.0000,  0.0000,  0.5000), GfVec3f( 0.0000,  0.0000,  0.5000),
        GfVec3f( 0.0000,  0.0000,  0.5000) };

    // Get implicit geom scale transform
    GfMatrix4d scale = exportCylindricalTransform<UsdGeomCone>(prim, node, frame);
    for (GfVec3f& pt : points)
        pt = scale.Transform(pt);

    UsdGeomCone cone(prim);
    _ReadSidedness(cone, node, frame);
    _ReadPointsAndVertices(node, numVerts, verts, points);
    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    ReadMeshLight(prim, context, node, time);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);
    return nullptr;
}

// Note that we don't have capsule shapes in Arnold. Do we want to make a
// special case, and combine cylinders with spheres, or is it enough for now ?
AtNode* UsdArnoldReadCapsule::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;
    AtNode *node = context.CreateArnoldNode("polymesh", prim.GetPath().GetText());
    AiNodeSetBool(node, str::smoothing, true);
    
    // slices are segments around the mesh
    static constexpr int _capsuleSlices = 10;
    // stacks are segments along the spine axis
    static constexpr int _capsuleStacks = 1;
    // capsules have additional stacks along the spine for each capping hemisphere
    static constexpr int _capsuleCapStacks = 4;

    const int numCounts =
        _capsuleSlices * (_capsuleStacks + 2 * _capsuleCapStacks);
    const int numIndices =
        4 * _capsuleSlices * _capsuleStacks             // cylinder quads
        + 4 * 2 * _capsuleSlices * (_capsuleCapStacks-1)  // hemisphere quads
        + 3 * 2 * _capsuleSlices;                         // end cap tris

        VtIntArray numVerts(numCounts);
        int * counts = numVerts.data();

        VtIntArray verts(numIndices);
        int * indices = verts.data();

        // populate face counts and face indices
        int face = 0, index = 0, ptr = 0;

        // base hemisphere end cap triangles
        int base = ptr++;
        for (int i=0; i<_capsuleSlices; ++i) {
            counts[face++] = 3;
            indices[index++] = ptr + (i+1)%_capsuleSlices;
            indices[index++] = ptr + i;
            indices[index++] = base;
        }

        // middle and hemisphere quads
        for (int i=0; i<_capsuleStacks+2*(_capsuleCapStacks-1); ++i) {
            for (int j=0; j<_capsuleSlices; ++j) {
                float x0 = 0;
                float x1 = x0 + _capsuleSlices;
                float y0 = j;
                float y1 = (j + 1) % _capsuleSlices;
                counts[face++] = 4;
                indices[index++] = ptr + x0 + y0;
                indices[index++] = ptr + x0 + y1;
                indices[index++] = ptr + x1 + y1;
                indices[index++] = ptr + x1 + y0;
            }
            ptr += _capsuleSlices;
        }

        // top hemisphere end cap triangles
        int top = ptr + _capsuleSlices;
        for (int i=0; i<_capsuleSlices; ++i) {
            counts[face++] = 3;
            indices[index++] = ptr + i;
            indices[index++] = ptr + (i+1)%_capsuleSlices;
            indices[index++] = top;
        }

    UsdGeomCapsule capsule(prim);

    // Get implicit geom scale transform
    VtValue heightValue;
    if (!capsule.GetHeightAttr().Get(&heightValue, frame))
        AiMsgWarning("Could not evaluate height attribute on prim %s",
            prim.GetPath().GetText());
    float height = VtValueGetFloat(heightValue);

    VtValue radiusValue;
    if (!capsule.GetRadiusAttr().Get(&radiusValue, frame))
        AiMsgWarning("Could not evaluate radius attribute on prim %s",
            prim.GetPath().GetText());
    float radius = VtValueGetFloat(radiusValue);

    TfToken axis = UsdGeomTokens->z;
    if (!capsule.GetAxisAttr().Get(&axis, frame))
        AiMsgWarning("Could not evaluate axis attribute on prim %s",
            prim.GetPath().GetText());

    // choose basis vectors aligned with the spine axis
    GfVec3f u, v, spine;
    if (axis == UsdGeomTokens->x) {
        u = GfVec3f::YAxis();
        v = GfVec3f::ZAxis();
        spine = GfVec3f::XAxis();
    } else if (axis == UsdGeomTokens->y) {
        u = GfVec3f::ZAxis();
        v = GfVec3f::XAxis();
        spine = GfVec3f::YAxis();
    } else { // (axis == UsdGeomTokens->z)
        u = GfVec3f::XAxis();
        v = GfVec3f::YAxis();
        spine = GfVec3f::ZAxis();
    }

    // compute a ring of points with unit radius in the uv plane
    std::vector<GfVec3f> ring(_capsuleSlices);
    for (int i=0; i<_capsuleSlices; ++i) {
        float a = float(2 * M_PI * i) / _capsuleSlices;
        ring[i] = u * cosf(a) + v * sinf(a);
    }

    const int numPoints =
        _capsuleSlices * (_capsuleStacks + 1)       // cylinder
      + 2 * _capsuleSlices * (_capsuleCapStacks-1)  // hemispheres
      + 2;                                          // end points

    // populate points
    VtVec3fArray points(numPoints);
    GfVec3f * p = points.data();

    // base hemisphere
    *p++ = spine * (-height/2-radius);
    for (int i=0; i<_capsuleCapStacks-1; ++i) {
        float a = float(M_PI / 2) * (1.0f - float(i+1) / _capsuleCapStacks);
        float r = radius * cosf(a);
        float w = radius * sinf(a);

        for (int j=0; j<_capsuleSlices; ++j) {
            *p++ = r * ring[j] + spine * (-height/2-w);
        }
    }

    // middle
    for (int i=0; i<=_capsuleStacks; ++i) {
        float t = float(i) / _capsuleStacks;
        float w = height * (t - 0.5f);

        for (int j=0; j<_capsuleSlices; ++j) {
            *p++ = radius * ring[j] + spine * w;
        }
    }

    // top hemisphere
    for (int i=0; i<_capsuleCapStacks-1; ++i) {
        float a = float(M_PI / 2) * (float(i+1) / _capsuleCapStacks);
        float r = radius * cosf(a);
        float w = radius * sinf(a);

        for (int j=0; j<_capsuleSlices; ++j) {
            *p++ = r *  ring[j] + spine * (height/2+w);
        }
    }
    *p++ = spine * (height/2.0f+radius);

    _ReadSidedness(capsule, node, frame);
    _ReadPointsAndVertices(node, numVerts, verts, points);
    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context);
    ReadMeshLight(prim, context, node, time);
    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");

    // Check the primitive visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);
    return node;
}

void ApplyInputMatrix(AtNode *node, const AtParamValueMap* params)
{
    if (params == nullptr)
        return;
    AtArray* parentMatrices = nullptr;
    if (!AiParamValueMapGetArray(params, str::matrix, &parentMatrices))
        return;
    if (parentMatrices == nullptr || AiArrayGetNumElements(parentMatrices) == 0)
        return;

    AtArray *matrix = AiNodeGetArray(node, str::matrix);
    AtMatrix m;
    if (matrix != nullptr && AiArrayGetNumElements(matrix) > 0)
        m = AiM4Mult(AiArrayGetMtx(parentMatrices, 0), AiArrayGetMtx(matrix, 0));
    else
        m = AiArrayGetMtx(parentMatrices, 0);

    AiArraySetMtx(matrix, 0, m);
}

AtNode* UsdArnoldReadBounds::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    if (!context.GetPrimVisibility(prim, frame))
        return nullptr;

    AtNode *node = context.CreateArnoldNode("box", prim.GetPath().GetText());
    if (!prim.IsA<UsdGeomBoundable>())
        return node;

    UsdGeomBoundable boundable(prim);
    VtVec3fArray extent;

    UsdGeomBoundable::ComputeExtentFromPlugins(boundable, UsdTimeCode(frame), &extent);

    AiNodeSetVec(node, str::_min, extent[0][0], extent[0][1], extent[0][2]);
    AiNodeSetVec(node, str::_max, extent[1][0], extent[1][1], extent[1][2]);
    ReadMatrix(prim, node, time, context);
    ApplyInputMatrix(node, _params);

    // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);
    return node;
}

AtNode* UsdArnoldReadGenericPolygons::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;
    // For some attributes, we should never try to read them with motion blur,
    // we use another timeSettings for them
    TimeSettings staticTime(time);
    staticTime.motionBlur = false;

    if (!context.GetPrimVisibility(prim, frame))
        return nullptr;

    AtNode *node = context.CreateArnoldNode("polymesh", prim.GetPath().GetText());

    if (!prim.IsA<UsdGeomMesh>())
        return node;

    UsdGeomMesh mesh(prim);
    MeshOrientation meshOrientation;
    // Get orientation. If Left-handed, we will need to invert the vertex
    // indices
    {
        TfToken orientationToken;
        if (mesh.GetOrientationAttr().Get(&orientationToken, frame)) {
            if (orientationToken == UsdGeomTokens->leftHanded) {
                meshOrientation.reverse = true;
                mesh.GetFaceVertexCountsAttr().Get(&meshOrientation.nsidesArray, frame);
            }
        }
    }
    ReadAttribute(mesh.GetFaceVertexCountsAttr(), node, "nsides", staticTime,
            context, AI_TYPE_ARRAY, AI_TYPE_BYTE);   

    if (!meshOrientation.reverse) {
        // Basic right-handed orientation, no need to do anything special here
        ReadAttribute(mesh.GetFaceVertexIndicesAttr(), node, "vidxs", staticTime,
            context, AI_TYPE_ARRAY, AI_TYPE_UINT);
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
            AiNodeSetArray(node, str::vidxs, AiArrayConvert(size, 1, AI_TYPE_UINT, arnold_vec.data()));
        } else
            AiNodeResetParameter(node, str::vidxs);
    }
    ReadAttribute(mesh.GetPointsAttr(), node, "vlist", time,
            context, AI_TYPE_ARRAY, AI_TYPE_VECTOR);
    ReadMatrix(prim, node, time, context);
    ApplyInputMatrix(node, _params);

    // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);

    return node;
}

AtNode* UsdArnoldReadGenericPoints::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    AtNode *node = context.CreateArnoldNode("points", prim.GetPath().GetText());

    if (!prim.IsA<UsdGeomPointBased>())
        return node;

    UsdGeomPointBased points(prim);
    ReadAttribute(points.GetPointsAttr(), node, "points", time,
            context, AI_TYPE_ARRAY, AI_TYPE_VECTOR);
    
    ReadMatrix(prim, node, time, context);
    ApplyInputMatrix(node, _params);

    // Check the primitive visibility, set the AtNode visibility to 0 if it's meant to be hidden
    if (!context.GetPrimVisibility(prim, frame))
        AiNodeSetByte(node, str::visibility, 0);
    return node;
}

class InstancerPrimvarsRemapper : public PrimvarsRemapper
{
public:
    InstancerPrimvarsRemapper() {}
    virtual ~InstancerPrimvarsRemapper() {}
    void RemapPrimvar(TfToken &name, std::string &interpolation) override;
    
private:
};

template <class T>
static bool CopyArrayElement(VtValue &value, unsigned int index)
{
    if (!value.IsHolding<VtArray<T>>())
        return false;

    VtArray<T> array = value.UncheckedGet<VtArray<T>>();
    if (index < array.size()) {
        value = array[index];
    }
    return true;
}
template <typename T0, typename T1, typename... T>
inline bool CopyArrayElement(VtValue &value, unsigned int index)
{
    return CopyArrayElement<T0>(value, index) || CopyArrayElement<T1, T...>(value, index);
}

void InstancerPrimvarsRemapper::RemapPrimvar(TfToken &name, std::string &interpolation)
{
    std::string instancerName = "instance_";
    instancerName += name.GetText();
    name = TfToken(instancerName.c_str());
    interpolation = "constant ARRAY";
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

AtNode* UsdArnoldReadPointInstancer::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    UsdArnoldReader *reader = context.GetReader();
    if (reader == nullptr)
        return nullptr;

    const TimeSettings &time = context.GetTimeSettings();
    float frame = time.frame;

    TimeSettings staticTime(time);
    staticTime.motionBlur = false;

    UsdGeomPointInstancer pointInstancer(prim);

    // this will be used later to contruct the name of the instances
    std::string primName = prim.GetPath().GetText();

    // get all proto paths (i.e. input nodes to be instantiated)
    SdfPathVector protoPaths;
    pointInstancer.GetPrototypesRel().GetTargets(&protoPaths);

    // get the visibility of each prototype, so that we can apply its visibility to all of its instances
    std::vector<unsigned char> protoVisibility(protoPaths.size(), AI_RAY_ALL);

    // get proto type index for all instances
    VtIntArray protoIndices;
    pointInstancer.GetProtoIndicesAttr().Get(&protoIndices, frame);

    // the size of the protoIndices array gives us the amount of instances
    size_t numInstances = protoIndices.size();

    if (numInstances == 0 || protoPaths.empty())
        return nullptr;

    AtNode *node = context.CreateArnoldNode("instancer", prim.GetPath().GetText());

    // initialize the nodes array to the proper size    
    std::vector<AtNode *> nodesVec(protoPaths.size(), nullptr);
    std::vector<std::string> nodesRefs(protoPaths.size());

    // We want to keep track of how which prototypes rely on a child usd procedural,
    // as they need to treat instance matrices differently
    std::vector<bool> nodesChildProcs(protoPaths.size(), false);    
    int numChildProc = 0;    
    std::vector<float> protoLightIntensities;

    for (size_t i = 0; i < protoPaths.size(); ++i) {
        const SdfPath &protoPath = protoPaths.at(i);
        // get the proto primitive, and ensure it's properly exported to arnold,
        // since we don't control the order in which nodes are read.
        UsdPrim protoPrim = reader->GetStage()->GetPrimAtPath(protoPath);

        // If some of the prototypes are lights we'll need to set a user data
        // instance_intensity, as we do for meshes visibility. 
        // There are currently different ways of having light primitives in USD.
        // Typed schemas can derive from UsdLuxBoundableLightBase or UsdLuxNonboundableLightBase.
        // But the LightAPI schema can also be applied to any primitive
        if (
#if PXR_VERSION >= 2111
            protoPrim.IsA<UsdLuxBoundableLightBase>() || 
            protoPrim.IsA<UsdLuxNonboundableLightBase>() 
#if PXR_VERSION >= 2302
            || protoPrim.HasAPI(_tokens->LightAPI)
#endif
#else
            protoPrim.IsA<UsdLuxLight>()      
#endif
            ) {
            
            // This prototype is a light, let's initialize our 
            // vector to a default intensity of 1
            if (protoLightIntensities.empty())
                protoLightIntensities.assign(protoPaths.size(), 1.f);

            // Get the intensity value from the light primitive
            float lightIntensity = 1.f;
            UsdAttribute intensityAttr = protoPrim.GetAttribute(str::t_inputs_intensity);
            VtValue intensityValue;
            if (intensityAttr && intensityAttr.Get(&intensityValue, frame)) {
                protoLightIntensities[i] = VtValueGetFloat(intensityValue);
            }
        }

        std::string objType = (protoPrim) ? protoPrim.GetTypeName().GetText() : "";

        if (protoPrim)
        {
            // Compute the USD visibility of this prototype. If it's hidden, we want all its instances
            // to be hidden too #458
            if (!IsPrimVisible(protoPrim, reader, frame)) {
                protoVisibility[i] = 0;
            }
        }

        // I need to create a new proto node in case this primitive isn't directly translated as an Arnold AtNode.
        // As of now, this only happens for Xform or non-typed prims, so I'm checking for these types,
        // and also I'm verifying if the registry is able to read nodes of this type.
        // In the future we might want to make this more robust, we could eventually add a function in
        // the primReader telling us if this primitive will generate an arnold node with the same name or not.
        bool createProto = (objType == "Xform" || objType.empty() ||
             (reader->GetRegistry()->GetPrimReader(objType) == nullptr));

        if (createProto) {
            // There's no AtNode for this proto, we need to create a usd procedural that loads
            // the same usd file but points only at this object path

            nodesVec[i] = reader->CreateNestedProc(protoPath.GetText(), context);

            // we keep track that this prototype relies on a child usd procedural
            nodesChildProcs[i] = true;
            numChildProc++;
        } else {
            nodesRefs[i] = protoPath.GetText();
        }
    }
    AiNodeSetArray(node, str::nodes, AiArrayConvert(nodesVec.size(), 1, AI_TYPE_NODE, &nodesVec[0]));
    for (unsigned i = 0; i < nodesRefs.size(); ++i) {
        if (nodesRefs[i].empty())
            continue;
        std::string nodesAttrElem = TfStringPrintf("nodes[%d]", i);
        context.AddConnection(
            node, nodesAttrElem, nodesRefs[i], ArnoldAPIAdapter::CONNECTION_PTR);
    }
    
    std::vector<UsdTimeCode> times;
    if (time.motionBlur) {
        int numKeys = GetTimeSampleNumKeys(prim, time, TfToken("instance")); // to be coherent with the delegate
        if (numKeys > 1) {
            for (int i = 0; i < numKeys; ++i) {
                times.push_back(time.frame + time.motionStart + i * (time.motionEnd - time.motionStart) / (numKeys-1));
            }
        }
    }
    if (times.empty()) {
        times.push_back(frame);
    }
    std::vector<bool> pruneMaskValues = pointInstancer.ComputeMaskAtTime(frame);
    if (!pruneMaskValues.empty() && pruneMaskValues.size() != numInstances) {
        // If the amount of prune mask elements doesn't match the amount of instances,
        // then something is wrong. We dump an error and clear the mask vector.
        AiMsgError("[usd] Point instancer %s : Mismatch in length of indices and mask", primName.c_str());
        pruneMaskValues.clear();
    }

    // Usually we'd get all the instance matrices, taking into account the prototype's transform (IncludeProtoXform),
    // and the arnold instances will be created with inherit_xform = false. But when the prototype is a child usd proc
    // then this doesn't work as inherit_xform will ignore the matrix of the child usd proc itself. The transform of the
    // root primitive will still be applied, so we will get double transformations #956

    // So, if all prototypes are child procs, we just need to call ComputeInstanceTransformsAtTimes 
    // with the ExcludeProtoXform flag
    std::vector<VtArray<GfMatrix4d> > xformsArray;
    pointInstancer.ComputeInstanceTransformsAtTimes(&xformsArray, times, frame, (numChildProc == (int) protoPaths.size()) ?
                UsdGeomPointInstancer::ExcludeProtoXform : UsdGeomPointInstancer::IncludeProtoXform, 
                UsdGeomPointInstancer::IgnoreMask);

    // However, if some prototypes are child procs AND other prototypes are simple geometries, then we need 
    // to get both instance matrices with / without the prototype xform and use the appropriate one.
    // Note that this can seem overkill, but the assumption is that in practice this use case shouldn't be 
    // the most frequent one
    std::vector<VtArray<GfMatrix4d> > excludedXformsArray;
    bool mixedProtos = numChildProc > 0 && numChildProc < (int) protoPaths.size();
    if (mixedProtos) {
        pointInstancer.ComputeInstanceTransformsAtTimes(&excludedXformsArray, times, frame, 
                UsdGeomPointInstancer::ExcludeProtoXform, UsdGeomPointInstancer::IgnoreMask);
    }

    unsigned int numKeys = xformsArray.size();
    std::vector<unsigned char> instanceVisibilities(numInstances, AI_RAY_ALL);
    std::vector<unsigned int> instanceIdxs(numInstances, 0);

    std::vector<float> instanceIntensities;
    if (!protoLightIntensities.empty())
        instanceIntensities.assign(numInstances, 1.f);

    // Create a big matrix array with all the instance matrices for the first key, 
    // then all matrices for the second key, etc..
    std::vector<AtMatrix> instance_matrices(numKeys * numInstances);
    for (size_t i = 0; i < numInstances; ++i) {
        // This instance has to be pruned, let's skip it
        if ((!pruneMaskValues.empty() && pruneMaskValues[i] == false) || 
            (protoIndices[i] >= (int) protoVisibility.size()))
            instanceVisibilities[i] = 0;
        else {
            instanceVisibilities[i] = protoVisibility[protoIndices[i]];
            if (!protoLightIntensities.empty())
                instanceIntensities[i] = protoLightIntensities[protoIndices[i]];
        }

        // loop over all the motion steps and append the matrices as a big list of floats
        for (size_t t = 0; t < numKeys; ++t) {

            // use the proper matrix, that was computed either with/without the proto's xform.
            // It depends on whether the prototype is a child usd proc or a simple geometry
            const double *matrixArray = (mixedProtos && nodesChildProcs[protoIndices[i]]) ? 
                excludedXformsArray[t][i].GetArray() : xformsArray[t][i].GetArray();
            AtMatrix &matrix = instance_matrices[i + t * numInstances];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j, matrixArray++)
                    matrix[i][j] = (float)*matrixArray;
        }
        instanceIdxs[i] = protoIndices[i];
    }
    AiNodeSetArray(node, str::instance_matrix, AiArrayConvert(numInstances, numKeys, AI_TYPE_MATRIX, &instance_matrices[0]));
    AiNodeSetArray(node, str::instance_visibility, AiArrayConvert(numInstances, 1, AI_TYPE_BYTE, &instanceVisibilities[0]));
    AiNodeSetArray(node, str::node_idxs, AiArrayConvert(numInstances, 1, AI_TYPE_UINT, &instanceIdxs[0]));

    // If some of the prototypes are lights, we need to set the instance_intensity user data
    // because the source prototype has its intensity set to 0
    if (!instanceIntensities.empty()) {
        AiNodeDeclare(node, str::instance_intensity, "constant ARRAY FLOAT");
        AiNodeSetArray(node, str::instance_intensity, AiArrayConvert(numInstances, 1, AI_TYPE_FLOAT, &instanceIntensities[0]));
    }

    ReadMatrix(prim, node, time, context);
    InstancerPrimvarsRemapper primvarsRemapper;
    // For instancer primvars, we want to remove motion blur as it's causing errors #1298
    ReadPrimvars(prim, node, staticTime, context, &primvarsRemapper);
    ReadMaterialBinding(prim, node, context, false); // don't assign the default shader

    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");
    // Check the prim visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetByte(node, str::visibility, 0);

    AiNodeSetFlt(node, str::motion_start, time.motionStart);
    AiNodeSetFlt(node, str::motion_end, time.motionEnd);
    return node;
}

AtNode* UsdArnoldReadVolume::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    UsdArnoldReader *reader = context.GetReader();
    if (reader == nullptr)
        return nullptr;

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
        UsdPrim fieldPrim = reader->GetStage()->GetPrimAtPath(it->second);
        if (!fieldPrim || !fieldPrim.IsA<UsdVolOpenVDBAsset>()) {
            AiMsgWarning("[usd] Volume field primitive is invalid %s", it->second.GetText());
            continue;
        }
        UsdVolOpenVDBAsset vdbAsset(fieldPrim);

        VtValue vdbFilePathValue;

        const UsdAttribute& filePathAttr(vdbAsset.GetFilePathAttr());
        if (filePathAttr.Get(&vdbFilePathValue, time.frame)) {
            std::string fieldFilename = VtValueGetString(vdbFilePathValue);
            if (filename.empty())
                filename = fieldFilename;
            else if (fieldFilename != filename) {
                AiMsgWarning("[usd] %s: arnold volume nodes only support a single .vdb file. ", AiNodeGetName(node));
            }
            TfToken vdbGrid;
            if (vdbAsset.GetFieldNameAttr().Get(&vdbGrid, time.frame)) {
                grids.push_back(vdbGrid);
            }
        }
    }

    // Now set the first vdb filename that was found
    AiNodeSetStr(node, str::filename, AtString(filename.c_str()));

    // Set all the grids that are needed
    AtArray *gridsArray = AiArrayAllocate(grids.size(), 1, AI_TYPE_STRING);
    for (size_t i = 0; i < grids.size(); ++i) {
        AiArraySetStr(gridsArray, i, AtString(grids[i].c_str()));
    }
    AiNodeSetArray(node, str::grids, gridsArray);

    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context, false); // don't assign the default shader

    ReadArnoldParameters(prim, context, node, time, "primvars:arnold");
    // Check the prim visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, time.frame))
        AiNodeSetByte(node, str::visibility, 0);

    return node;
}

AtNode* UsdArnoldReadProceduralCustom::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    // This schema is meant for custom procedurals. Its attribute "node_entry" will
    // indicate what is the node entry name for this node.
    UsdAttribute attr = prim.GetAttribute(str::t_arnold_node_entry);
    // for backward compatibility, check the attribute without namespace
    if (!attr)
        attr = prim.GetAttribute(str::t_node_entry);
    
    const TimeSettings &time = context.GetTimeSettings();
    VtValue value;
    // If the attribute "node_entry" isn't defined, we don't know what type of node
    // to create, so there is nothing we can do
    if (!attr || !attr.Get(&value, time.frame)) {
        return nullptr;
    }
    std::string nodeType = VtValueGetString(value);
    AtNode *node = context.CreateArnoldNode(nodeType.c_str(), prim.GetPath().GetText());
    
    ReadMatrix(prim, node, time, context);
    ReadPrimvars(prim, node, time, context);
    ReadMaterialBinding(prim, node, context, false); // don't assign the default shader
    ReadArnoldParameters(prim, context, node, time, "arnold");

    // Check the prim visibility, set the AtNode visibility to 0 if it's hidden
    if (!context.GetPrimVisibility(prim, time.frame)) {
        AiNodeSetByte(node, str::visibility, 0);
    }
    return node;
}

AtNode* UsdArnoldReadProcViewport::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    UsdArnoldReader *reader = context.GetReader();
    if (reader == nullptr)
        return nullptr;

    AtUniverse *universe = reader->GetUniverse();
    const TimeSettings &time = context.GetTimeSettings();

    std::string filename;
    std::string nodeType = _procName;

    if (!_procName.empty()) {
        // Get the filename of this ass/usd/abc procedural
        UsdAttribute attr = prim.GetAttribute(str::t_arnold_filename);
        // for backward compatibility, check the attribute without namespace
        if (!attr)
            attr = prim.GetAttribute(str::t_filename);

        VtValue value;
        if (!attr || !attr.Get(&value, time.frame)) {
            return nullptr;
        }
        filename = VtValueGetString(value);
    } else {
        // There's not a determined procedural node type, this is a custom procedural.
        // We get this information from the attribute "node_entry"
        UsdAttribute attr = prim.GetAttribute(str::t_arnold_node_entry);
        // for backward compatibility, check the attribute without namespace
        if (!attr)
            attr = prim.GetAttribute(str::t_node_entry);

        VtValue value;
        if (!attr || !attr.Get(&value, time.frame)) {
            return nullptr;
        }
        nodeType = VtValueGetString(value);
    }

    // create a temporary universe to create a dummy procedural
    AtUniverse *tmpUniverse = AiUniverse();

    // copy the procedural search path string from the input universe
    AiNodeSetStr(
        AiUniverseGetOptions(tmpUniverse), str::procedural_searchpath,
        AiNodeGetStr(AiUniverseGetOptions(universe), str::procedural_searchpath));

    // Create a procedural with the given node type
    AtNode *proc = AiNode(tmpUniverse, AtString(nodeType.c_str()), AtString("viewport_proc"));

    // Set the eventual filename
    if (!filename.empty()) {
        AiNodeSetStr(proc, str::filename, AtString(filename.c_str()));
    }
    // read the matrix and apply the eventual input one from the AtParamsValueMap
    // This node's matrix won't be taken into account but we'll apply it to the params map
    ReadMatrix(prim, proc, time, context);
    ApplyInputMatrix(proc, _params);
    bool setMatrixParam = false;
    AtArray *matrices = AiNodeGetArray(proc, str::matrix);
    if (matrices && AiArrayGetNumElements(matrices) > 0)
        setMatrixParam = (!AiM4IsIdentity(AiArrayGetMtx(matrices, 0)));

    // ensure we read all the parameters from the procedural
    ReadArnoldParameters(prim, context, proc, time, "arnold");
    ReadPrimvars(prim, proc, time, context);

    AtParamValueMap *params =
            (_params) ? AiParamValueMapClone(_params) : AiParamValueMap();
    AiParamValueMapSetInt(params, str::mask, AI_NODE_SHAPE);
    // if needed, propagate the matrix to the child nodes
    if (setMatrixParam)
        AiParamValueMapSetArray(params, str::matrix, matrices);

    AiProceduralViewport(proc, universe, _mode, params);
    AiParamValueMapDestroy(params);

    AiUniverseDestroy(tmpUniverse);
    return nullptr;
}
