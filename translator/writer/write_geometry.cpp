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
#include "write_geometry.h"

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

void UsdArnoldWriteMesh::write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = getArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.getUsdStage();    // Get the USD stage defined in the writer
    
    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, SdfPath(nodeName));
    UsdPrim prim = mesh.GetPrim();

    writeMatrix(mesh, node, writer);    
    writeAttribute(node, "vlist", prim, mesh.GetPointsAttr(), writer);    

    mesh.GetOrientationAttr().Set(UsdGeomTokens->rightHanded);
    AtArray *vidxs = AiNodeGetArray(node, "vidxs");
    if (vidxs) {
        unsigned int nelems = AiArrayGetNumElements(vidxs);
        VtArray<int> vtArr(nelems);
        for (unsigned int i = 0; i < nelems; ++i) {
            vtArr[i] = (int)AiArrayGetUInt(vidxs, i);
        }
        mesh.GetFaceVertexIndicesAttr().Set(vtArr);
    }
    _exportedAttrs.insert("vidxs");
    AtArray *nsides = AiNodeGetArray(node, "nsides");
    if (nsides) {
        unsigned int nelems = AiArrayGetNumElements(nsides);
        VtArray<int> vtArr(nelems);
        for (unsigned int i = 0; i < nelems; ++i) {
            vtArr[i] = (unsigned char) AiArrayGetUInt(nsides, i);
        }
        mesh.GetFaceVertexCountsAttr().Set(vtArr);
    }
    _exportedAttrs.insert("nsides");

    // export UVs
    AtArray *uvlist = AiNodeGetArray(node, "uvlist");
    static TfToken uvToken("uv");
    unsigned int uvlistNumElems = (uvlist) ? AiArrayGetNumElements(uvlist) : 0;
    if (uvlistNumElems > 0) {
        UsdGeomPrimvarsAPI primvarAPI(prim);
        UsdGeomPrimvar uvPrimVar = mesh.CreatePrimvar(uvToken,
                                 SdfValueTypeNames->Float2Array,
                                 UsdGeomTokens->faceVarying,
                                 uvlistNumElems);

        VtArray<GfVec2f> uvValues(uvlistNumElems);
        AtVector2 *uvArrayValues = static_cast<AtVector2*>(AiArrayMap(uvlist));
        for (unsigned int i = 0; i < uvlistNumElems; ++i) {
            uvValues[i] = GfVec2f(uvArrayValues[i].x, uvArrayValues[i].y);
        }
        uvPrimVar.Set(uvValues);
        AiArrayUnmap(uvlist);
        
        // check if the indices are present
        AtArray *uvidxsArray = AiNodeGetArray(node, "uvidxs");
        unsigned int uvidxsSize = (uvidxsArray) ? AiArrayGetNumElements(uvidxsArray) : 0;
        if (uvidxsSize > 0) {
            uint32_t *uvidxs  = static_cast<uint32_t*>(AiArrayMap(uvidxsArray));
        
            VtIntArray vtIndices(uvidxsSize);
            for (unsigned int i = 0; i < uvidxsSize; ++i) {
                vtIndices[i] = uvidxs[i];
            }
            uvPrimVar.SetIndices(vtIndices);
            AiArrayUnmap(uvidxsArray);
        }
    }
    AtArray *nlist = AiNodeGetArray(node, "nlist");
    static TfToken normalsToken("normals");
    unsigned int nlistNumElems = (nlist) ? AiArrayGetNumElements(nlist) : 0;
    if (nlistNumElems > 0) {
        UsdGeomPrimvarsAPI primvarAPI(prim);
        UsdGeomPrimvar normalsPrimVar = mesh.CreatePrimvar(normalsToken,
                                 SdfValueTypeNames->Vector3fArray,
                                 UsdGeomTokens->faceVarying,
                                 nlistNumElems);

        VtArray<GfVec3f> normalsValues(nlistNumElems);
        AtVector *nlistArrayValues = static_cast<AtVector*>(AiArrayMap(nlist));
        for (unsigned int i = 0; i < nlistNumElems; ++i) {
            normalsValues[i] = GfVec3f(nlistArrayValues[i].x, nlistArrayValues[i].y, nlistArrayValues[i].z);
        }
        normalsPrimVar.Set(normalsValues);
        AiArrayUnmap(nlist);
        
        // check if the indices are present
        AtArray *nidxsArray = AiNodeGetArray(node, "nidxs");
        unsigned int nidxsSize = (nidxsArray) ? AiArrayGetNumElements(nidxsArray) : 0;
        if (nidxsSize > 0) {
            uint32_t *nidxs  = static_cast<uint32_t*>(AiArrayMap(nidxsArray));        
            VtIntArray vtIndices(nidxsSize);
            for (unsigned int i = 0; i < nidxsSize; ++i) {
                vtIndices[i] = nidxs[i];
            }
            normalsPrimVar.SetIndices(vtIndices);
            AiArrayUnmap(nidxsArray);
        }
    }
    _exportedAttrs.insert("uvlist");
    _exportedAttrs.insert("uvidxs");
    _exportedAttrs.insert("nlist");
    _exportedAttrs.insert("nidxs");
    
    writeMaterialBinding(node, prim, writer);
    writeArnoldParameters(node, writer, prim, "primvars:arnold");
}

void UsdArnoldWriteCurves::write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = getArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.getUsdStage();    // Get the USD stage defined in the writer
    
    UsdGeomBasisCurves curves = UsdGeomBasisCurves::Define(stage, SdfPath(nodeName));
    UsdPrim prim = curves.GetPrim();

    writeMatrix(curves, node, writer);  

    TfToken curveType = UsdGeomTokens->cubic;
    switch(AiNodeGetInt(node, "basis")) {
        case 0:
            curves.GetBasisAttr().Set(TfToken(UsdGeomTokens->bezier));
        break;
        case 1:
            curves.GetBasisAttr().Set(TfToken(UsdGeomTokens->bspline));
        break;
        case 2:
            curves.GetBasisAttr().Set(TfToken(UsdGeomTokens->catmullRom));
        break;
        default:
        case 3:
            curveType =  UsdGeomTokens->linear;
        break;
    }
    curves.GetTypeAttr().Set(curveType);

    writeAttribute(node, "points", prim, curves.GetPointsAttr(), writer);    
    writeAttribute(node, "num_points", prim, curves.GetCurveVertexCountsAttr(), writer);    
    writeAttribute(node, "radius", prim, curves.GetWidthsAttr(), writer);    
    writeMaterialBinding(node, prim, writer);
    writeArnoldParameters(node, writer, prim, "primvars:arnold");
}


void UsdArnoldWritePoints::write(const AtNode *node, UsdArnoldWriter &writer)
{
    std::string nodeName = getArnoldNodeName(node); // what is the USD name for this primitive
    UsdStageRefPtr stage = writer.getUsdStage();    // Get the USD stage defined in the writer
    
    UsdGeomPoints points = UsdGeomPoints::Define(stage, SdfPath(nodeName));
    UsdPrim prim = points.GetPrim();

    writeMatrix(points, node, writer);  

    writeAttribute(node, "points", prim, points.GetPointsAttr(), writer);    
    writeAttribute(node, "radius", prim, points.GetWidthsAttr(), writer);    
    writeMaterialBinding(node, prim, writer);
    writeArnoldParameters(node, writer, prim, "primvars:arnold");
}
