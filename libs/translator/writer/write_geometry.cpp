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
#include "write_geometry.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

void UsdArnoldWriteMesh::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);    
    writer.CreateHierarchy(objPath);
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, objPath);
    UsdPrim prim = mesh.GetPrim();

    _WriteMatrix(mesh, node, writer);
    WriteAttribute(node, "vlist", prim, mesh.GetPointsAttr(), writer);

    writer.SetAttribute(mesh.GetOrientationAttr(), UsdGeomTokens->rightHanded);    
    AtArray *vidxs = AiNodeGetArray(node, AtString("vidxs"));
    VtArray<int> vtArrIdxs;
    if (vidxs) {
        unsigned int nelems = AiArrayGetNumElements(vidxs);
        vtArrIdxs.resize(nelems);
        for (unsigned int i = 0; i < nelems; ++i) {
            vtArrIdxs[i] = (int)AiArrayGetUInt(vidxs, i);
        }
        writer.SetAttribute(mesh.GetFaceVertexIndicesAttr(), vtArrIdxs);
    }
    _exportedAttrs.insert("vidxs");
    AtArray *nsides = AiNodeGetArray(node, AtString("nsides"));
    VtArray<int> vtArrNsides;
    if (nsides) {
        unsigned int nelems = AiArrayGetNumElements(nsides);
        if (nelems > 0) {
            vtArrNsides.resize(nelems);
            for (unsigned int i = 0; i < nelems; ++i)
                vtArrNsides[i] = (unsigned char)AiArrayGetUInt(nsides, i);
        }
    }
    if (vtArrNsides.empty()) {
        // For arnold, empty nsides means that all the polygons are triangles.
        // But this won't be understood by USD, so we should create the array here.
        unsigned int nelems = vtArrIdxs.size() / 3;
        vtArrNsides.assign(nelems, 3);
    }
    writer.SetAttribute(mesh.GetFaceVertexCountsAttr(), vtArrNsides);
    _exportedAttrs.insert("nsides");

    // export UVs
    AtArray *uvlist = AiNodeGetArray(node, AtString("uvlist"));
    static TfToken uvToken("st");
    unsigned int uvlistNumElems = (uvlist) ? AiArrayGetNumElements(uvlist) : 0;
    if (uvlistNumElems > 0) {
        UsdGeomPrimvarsAPI primvarAPI(prim);
        UsdGeomPrimvar uvPrimVar = primvarAPI.CreatePrimvar(uvToken, SdfValueTypeNames->Float2Array, UsdGeomTokens->faceVarying, uvlistNumElems);

        VtArray<GfVec2f> uvValues(uvlistNumElems);
        AtVector2 *uvArrayValues = static_cast<AtVector2 *>(AiArrayMap(uvlist));
        for (unsigned int i = 0; i < uvlistNumElems; ++i) {
            uvValues[i] = GfVec2f(uvArrayValues[i].x, uvArrayValues[i].y);
        }
        writer.SetPrimVar(uvPrimVar, uvValues);
        AiArrayUnmap(uvlist);

        // check if the indices are present
        AtArray *uvidxsArray = AiNodeGetArray(node, AtString("uvidxs"));
        unsigned int uvidxsSize = (uvidxsArray) ? AiArrayGetNumElements(uvidxsArray) : 0;
        if (uvidxsSize > 0) {
            uint32_t *uvidxs = static_cast<uint32_t *>(AiArrayMap(uvidxsArray));

            VtIntArray vtIndices(uvidxsSize);
            for (unsigned int i = 0; i < uvidxsSize; ++i) {
                vtIndices[i] = uvidxs[i];
            }
            writer.SetPrimVarIndices(uvPrimVar, vtIndices);
            AiArrayUnmap(uvidxsArray);
        }
    }
    AtArray *nlist = AiNodeGetArray(node, AtString("nlist"));
    static TfToken normalsToken("normals");
    unsigned int nlistNumElems = (nlist) ? AiArrayGetNumElements(nlist) : 0;
    if (nlistNumElems > 0) {
        UsdGeomPrimvarsAPI primvarAPI(prim);
        UsdGeomPrimvar normalsPrimVar = primvarAPI.CreatePrimvar(
            normalsToken, SdfValueTypeNames->Vector3fArray, UsdGeomTokens->faceVarying, nlistNumElems);

        VtArray<GfVec3f> normalsValues(nlistNumElems);
        unsigned int nlistNumKeys = AiArrayGetNumKeys(nlist);
        AtVector *nlistArrayValues = static_cast<AtVector *>(AiArrayMap(nlist));

        if (nlistNumKeys > 1 && _motionStart < _motionEnd) {
            float timeDelta = (_motionEnd - _motionStart) / (int)(nlistNumKeys - 1);
            float time = _motionStart;

            for (unsigned int j = 0; j < nlistNumKeys; ++j, time += timeDelta) {
                memcpy(normalsValues.data(), nlistArrayValues, nlistNumElems * sizeof(GfVec3f));
                nlistArrayValues += nlistNumElems;
                writer.SetPrimVar(normalsPrimVar, normalsValues, &time);
            }

        } else {
            memcpy(normalsValues.data(), nlistArrayValues, nlistNumElems * sizeof(GfVec3f));
            writer.SetPrimVar(normalsPrimVar, normalsValues);
        }
        
        AiArrayUnmap(nlist);

        // check if the indices are present
        AtArray *nidxsArray = AiNodeGetArray(node, AtString("nidxs"));
        unsigned int nidxsSize = (nidxsArray) ? AiArrayGetNumElements(nidxsArray) : 0;
        if (nidxsSize > 0) {
            uint32_t *nidxs = static_cast<uint32_t *>(AiArrayMap(nidxsArray));
            VtIntArray vtIndices(nidxsSize);
            for (unsigned int i = 0; i < nidxsSize; ++i) {
                vtIndices[i] = nidxs[i];
            }
            writer.SetPrimVarIndices(normalsPrimVar, vtIndices);
            AiArrayUnmap(nidxsArray);
        }
    }
    AtString subdivType = AiNodeGetStr(node, AtString("subdiv_type"));
    static AtString catclarkStr("catclark");
    static AtString linearStr("linear");
    if (subdivType == catclarkStr)
        writer.SetAttribute(mesh.GetSubdivisionSchemeAttr(), UsdGeomTokens->catmullClark);
    else if (subdivType == linearStr)
        writer.SetAttribute(mesh.GetSubdivisionSchemeAttr(), UsdGeomTokens->bilinear);
    else
        writer.SetAttribute(mesh.GetSubdivisionSchemeAttr(), UsdGeomTokens->none);

    // always write subdiv iterations even if it's set to default
    UsdAttribute attr = prim.CreateAttribute(
        TfToken("primvars:arnold:subdiv_iterations"), SdfValueTypeNames->UChar, false);
    writer.SetAttribute(attr, AiNodeGetByte(node, AtString("subdiv_iterations")));

    // We're setting double sided to true if the sidedness is non-null.
    // Note that if it's not 255 (default), it will be set as a primvar
    // in _WriteArnoldParameters, and this primvar will have priority over
    // the double-sided boolean. This is why we're not setting sidedness
    // in the list of exportedAttrs
    if (AiNodeGetByte(node, AtString("sidedness")) > 0)
        writer.SetAttribute(mesh.GetDoubleSidedAttr(), true);

    _exportedAttrs.insert("uvlist");
    _exportedAttrs.insert("uvidxs");
    _exportedAttrs.insert("nlist");
    _exportedAttrs.insert("nidxs");
    
    _WriteMaterialBinding(node, prim, writer, AiNodeGetArray(node, AtString("shidxs")));
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");

    VtVec3fArray extent;
    if (UsdGeomBoundable::ComputeExtentFromPlugins(mesh, UsdTimeCode(_motionStart), &extent))
        writer.SetAttribute(mesh.GetExtentAttr(), extent);
    
}

void UsdArnoldWriteCurves::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);    
    writer.CreateHierarchy(objPath);
    UsdGeomBasisCurves curves = UsdGeomBasisCurves::Define(stage, objPath);
    UsdPrim prim = curves.GetPrim();

    _WriteMatrix(curves, node, writer);

    TfToken curveType = UsdGeomTokens->cubic;
    switch (AiNodeGetInt(node, AtString("basis"))) {
        case 0:
            writer.SetAttribute(curves.GetBasisAttr(), TfToken(UsdGeomTokens->bezier));
            break;
        case 1:
            writer.SetAttribute(curves.GetBasisAttr(), TfToken(UsdGeomTokens->bspline));
#if ARNOLD_VERSION_NUM >= 70103
            writer.SetAttribute(curves.GetWrapAttr(), TfToken(AiNodeGetStr(node, AtString("wrap_mode"))));
#endif
            break;
        case 2:
            writer.SetAttribute(curves.GetBasisAttr(), TfToken(UsdGeomTokens->catmullRom));
#if ARNOLD_VERSION_NUM >= 70103
            writer.SetAttribute(curves.GetWrapAttr(), TfToken(AiNodeGetStr(node, AtString("wrap_mode"))));
#endif
            break;
        default:
        case 3:
            curveType = UsdGeomTokens->linear;
            break;
    }
    writer.SetAttribute(curves.GetTypeAttr(), curveType);

    WriteAttribute(node, "points", prim, curves.GetPointsAttr(), writer);

    // num_points is an unsigned-int array in Arnold, but it's an int-array in USD
    // need to multiply the radius by 2 in order to get the width
    AtArray *numPointsArray = AiNodeGetArray(node, AtString("num_points"));
    unsigned int numPointsCount = (numPointsArray) ? AiArrayGetNumElements(numPointsArray) : 0;
    if (numPointsCount > 0) {
        VtArray<int> vertexCountArray(numPointsCount);
        unsigned int *in = static_cast<unsigned int *>(AiArrayMap(numPointsArray));
        for (unsigned int i = 0; i < numPointsCount; ++i) {
            vertexCountArray[i] = (int)in[i];
        }
        writer.SetAttribute(curves.GetCurveVertexCountsAttr(), vertexCountArray);
        AiArrayUnmap(numPointsArray);
    }
    _exportedAttrs.insert("num_points");

    // need to multiply the radius by 2 in order to get the width
    AtArray *radiusArray = AiNodeGetArray(node, AtString("radius"));
    unsigned int radiusCount = (radiusArray) ? AiArrayGetNumElements(radiusArray) : 0;
    if (radiusCount > 0) {
        VtArray<float> widthArray(radiusCount);
        float *in = static_cast<float *>(AiArrayMap(radiusArray));
        for (unsigned int i = 0; i < radiusCount; ++i) {
            widthArray[i] = in[i] * 2.f;
        }
        writer.SetAttribute(curves.GetWidthsAttr(), widthArray);
        AiArrayUnmap(radiusArray);

        if (radiusCount == 1)
            curves.SetWidthsInterpolation(UsdGeomTokens->constant);
        else
            curves.SetWidthsInterpolation(UsdGeomTokens->varying);
    }
    _exportedAttrs.insert("radius");

    _WriteMaterialBinding(node, prim, writer, AiNodeGetArray(node, AtString("shidxs")));
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
    VtVec3fArray extent;
    if (UsdGeomBoundable::ComputeExtentFromPlugins(curves, UsdTimeCode(_motionStart), &extent))
        writer.SetAttribute(curves.GetExtentAttr(), extent);

}

void UsdArnoldWritePoints::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // Get the USD stage defined in the writer
    SdfPath objPath(nodeName);    
    writer.CreateHierarchy(objPath);
    UsdGeomPoints points = UsdGeomPoints::Define(stage, objPath);
    UsdPrim prim = points.GetPrim();

    _WriteMatrix(points, node, writer);

    WriteAttribute(node, "points", prim, points.GetPointsAttr(), writer);

    // need to multiply the radius by 2 in order to get the width
    AtArray *radiusArray = AiNodeGetArray(node, AtString("radius"));
    unsigned int radiusCount = (radiusArray) ? AiArrayGetNumElements(radiusArray) : 0;
    if (radiusCount > 0) {
        VtArray<float> widthArray(radiusCount);
        float *in = static_cast<float *>(AiArrayMap(radiusArray));
        for (unsigned int i = 0; i < radiusCount; ++i) {
            widthArray[i] = in[i] * 2.f;
        }
        writer.SetAttribute(points.GetWidthsAttr(), widthArray);
        AiArrayUnmap(radiusArray);
    }
    _exportedAttrs.insert("radius");

    _WriteMaterialBinding(node, prim, writer);
    _WriteArnoldParameters(node, writer, prim, "primvars:arnold");
    VtVec3fArray extent;
    if (UsdGeomBoundable::ComputeExtentFromPlugins(points, UsdTimeCode(_motionStart), &extent))
        writer.SetAttribute(points.GetExtentAttr(), extent);
}
void UsdArnoldWriteProceduralCustom::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = GetArnoldNodeName(node, writer); // what should be the name of this USD primitive
    UsdStageRefPtr stage = writer.GetUsdStage();    // get the current stage defined in the writer
    SdfPath objPath(nodeName);
    _exportedAttrs.insert("name");

    // All custom procedurals are written as ArnoldProceduralCustom schema
    writer.CreateHierarchy(objPath);
    UsdPrim prim = stage->DefinePrim(objPath, TfToken("ArnoldProceduralCustom"));

    // Set the procedural node entry name as an attribute "arnold:node_entry"
    UsdAttribute nodeTypeAttr = prim.CreateAttribute(TfToken("arnold:node_entry"), SdfValueTypeNames->String, false);
    writer.SetAttribute(nodeTypeAttr, VtValue(_nodeEntry));

    UsdGeomXformable xformable(prim);
    _WriteMatrix(xformable, node, writer);
    _WriteMaterialBinding(node, prim, writer);
    _WriteArnoldParameters(node, writer, prim, "arnold");    

// For procedurals, we also want to write out the extents attribute
    AtUniverse *universe = AiUniverse();
    AtParamValueMap *params = AiParamValueMap();
    AiParamValueMapSetInt(params, AtString("mask"), AI_NODE_SHAPE);
    if (AiProceduralViewport(node, universe, AI_PROC_BOXES, params)) {
        AtBBox bbox;
        bbox.init();
        static AtString boxStr("box");

        // Need to loop over all the nodes that were created in this "viewport" 
        // universe, and get an expanded bounding box
        AtNodeIterator* nodeIter = AiUniverseGetNodeIterator(universe, AI_NODE_SHAPE);
        while (!AiNodeIteratorFinished(nodeIter)) {
            AtNode *node = AiNodeIteratorGetNext(nodeIter);
            if (AiNodeIs(node, boxStr)) {
                bbox.expand(AiNodeGetVec(node, AtString("min")));
                bbox.expand(AiNodeGetVec(node, AtString("max")));
            }
        }
        AiNodeIteratorDestroy(nodeIter);
        
        VtVec3fArray extent;
        extent.resize(2);
        extent[0][0] = bbox.min.x;
        extent[0][1] = bbox.min.y;
        extent[0][2] = bbox.min.z;
        extent[1][0] = bbox.max.x;
        extent[1][1] = bbox.max.y;
        extent[1][2] = bbox.max.z;
        UsdGeomBoundable boundable(prim);
        writer.SetAttribute(boundable.CreateExtentAttr(), extent);
    }

    AiParamValueMapDestroy(params);
    AiUniverseDestroy(universe);
}
