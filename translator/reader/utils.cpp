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
#include "utils.h"

#include <ai.h>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "reader.h"
#include "../arnold_usd.h"

#if USED_USD_VERSION_GREATER_EQ(20, 2)
#include <pxr/usd/usdShade/materialBindingAPI.h>
#endif

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE


static inline void getMatrix(const UsdPrim &prim, AtMatrix &matrix, float frame, UsdArnoldReaderContext &context)
{
    GfMatrix4d xform;
    bool dummyBool = false;
    UsdGeomXformCache *xformCache = context.getXformCache(frame);
    
    bool createXformCache = (xformCache == NULL);
    if (createXformCache)
        xformCache = new UsdGeomXformCache(frame);
    
    xform = xformCache->GetLocalToWorldTransform(prim);
    
    if (createXformCache)
        delete xformCache;

    const double *array = xform.GetArray();
    for (unsigned int i = 0; i < 4; ++i)
        for (unsigned int j = 0; j < 4; ++j)
            matrix[i][j] = array[4 * i + j];
}
/** Export Xformable transform as an arnold shape "matrix"
 */
void exportMatrix(const UsdPrim &prim, AtNode *node, const TimeSettings &time, UsdArnoldReaderContext &context)
{
    UsdGeomXformable xformable(prim);
    bool animated = xformable.TransformMightBeTimeVarying();
    AtMatrix matrix;
    if (time.motion_blur && animated) {
        // animated matrix, need to make it an array
        GfInterval interval(time.start(), time.end(), false, false);
        std::vector<double> timeSamples;
        xformable.GetTimeSamplesInInterval(interval, &timeSamples);
        size_t numKeys = AiMax(int(timeSamples.size()), (int)1);
        numKeys += 2; // need to add the start end end keys (interval has open bounds)
        AtArray *array = AiArrayAllocate(1, numKeys, AI_TYPE_MATRIX);
        float timeStep = float(interval.GetMax() - interval.GetMin()) / int(numKeys - 1);
        float timeVal = interval.GetMin();
        for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
            getMatrix(prim, matrix, timeVal, context);
            AiArraySetMtx(array, i, matrix);
        }
        AiNodeSetArray(node, "matrix", array);
        AiNodeSetFlt(node, "motion_start", time.motion_start);
        AiNodeSetFlt(node, "motion_end", time.motion_end);
    } else {
        getMatrix(prim, matrix, time.frame, context);
        // set the attribute
        AiNodeSetMatrix(node, "matrix", matrix);
    }
}

/**
 *  Export all primvars from this shape, and set them as arnold user data
 *
 **/

void exportPrimvars(const UsdPrim &prim, AtNode *node, const TimeSettings &time, MeshOrientation *orientation)
{
    assert(prim);
    UsdGeomImageable imageable = UsdGeomImageable(prim);
    assert(imageable);
    float frame = time.frame;

    for (const UsdGeomPrimvar &primvar : imageable.GetPrimvars()) {
        TfToken name;
        SdfValueTypeName typeName;
        TfToken interpolation;
        int elementSize;

        primvar.GetDeclarationInfo(&name, &typeName, &interpolation, &elementSize);
        
        // if we find a namespacing in the primvar name we skip it.
        // It's either an arnold attribute or it could be meant for another renderer
        if (name.GetString().find(':') != std::string::npos)
            continue;

        // Resolve the value
        VtValue vtValue;
        VtIntArray vtIndices;
        if (interpolation == UsdGeomTokens->constant) {
            if (!primvar.Get(&vtValue, frame)) {
                continue;
            }
        } else if (interpolation == UsdGeomTokens->faceVarying && primvar.IsIndexed()) {
            // It's an indexed value. We don't want to flatten it because it
            // breaks subdivs.
            if (!primvar.Get(&vtValue, frame)) {
                continue;
            }

            if (!primvar.GetIndices(&vtIndices, frame)) {
                continue;
            }
        } else {
            // USD comments suggest using using the ComputeFlattened() API
            // instead of Get even if they produce the same data.
            if (!primvar.ComputeFlattened(&vtValue, frame)) {
                continue;
            }
        }

        if (vtValue.IsHolding<VtArray<GfVec2f>>())
            exportPrimvar<GfVec2f>(vtValue, vtIndices, name, typeName, interpolation, prim, node, orientation);
        else if (vtValue.IsHolding<VtArray<GfVec3f>>())
            exportPrimvar<GfVec3f>(vtValue, vtIndices, name, typeName, interpolation, prim, node, orientation);
        else if (vtValue.IsHolding<VtArray<float>>())
            exportPrimvar<float>(vtValue, vtIndices, name, typeName, interpolation, prim, node, orientation);
        else if (vtValue.IsHolding<VtArray<int>>())
            exportPrimvar<int>(vtValue, vtIndices, name, typeName, interpolation, prim, node, orientation);
    }
}

// Export the materials / shaders assigned to a shape (node)
void exportMaterialBinding(const UsdPrim &prim, AtNode *node, UsdArnoldReaderContext &context)
{
#if USED_USD_VERSION_GREATER_EQ(20, 2)
    UsdShadeMaterial mat = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
#else
    UsdShadeMaterial mat = UsdShadeMaterial::GetBoundMaterial(prim);
#endif
    if (!mat) {
        AiNodeSetPtr(node, "shader", context.getReader()->getDefaultShader());
        return;
    }
    
    AtNode *shader = nullptr;
    TfToken arnoldContext("arnold");
    UsdShadeShader surface = mat.ComputeSurfaceSource(arnoldContext);
    if (!surface)
        surface = mat.ComputeSurfaceSource();

    if (surface) {
        context.addConnection(node, "shader", surface.GetPath().GetText(), UsdArnoldReaderContext::CONNECTION_PTR);
    }

    // We have a single "shader" binding in arnold, whereas USD has "surface"
    // and "volume" For now we export volume only if surface is empty.
    if (shader == nullptr) {
        UsdShadeShader volume = mat.ComputeVolumeSource(arnoldContext);
        if (!volume)
            volume = mat.ComputeVolumeSource();

        if (volume) {
            context.addConnection(node, "shader", volume.GetPath().GetText(), UsdArnoldReaderContext::CONNECTION_PTR);
        }

        // Shader is still null, let's assign the default one
        if (shader == nullptr) {
            AiNodeSetPtr(node, "shader", context.getReader()->getDefaultShader());
        }
    }

    // Now export displacement
    if (AiNodeIs(node, AtString("polymesh"))) {
        UsdShadeShader displacement = mat.ComputeDisplacementSource(arnoldContext);
        if (!displacement)
            displacement = mat.ComputeDisplacementSource();

        if (displacement) {
            context.addConnection(node, "disp_map", displacement.GetPath().GetText(), UsdArnoldReaderContext::CONNECTION_PTR);
        }
    }
}

/**
 * Export a specific shader parameter from USD to Arnold
 *
 **/


void exportParameter(
    UsdShadeShader &shader, AtNode *node, const std::string &usdName, const std::string &arnoldName,
    UsdArnoldReaderContext &context)
{
    if (node == NULL)
        return;

    const AtNodeEntry *nentry = AiNodeGetNodeEntry(node);
    const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nentry, AtString(arnoldName.c_str()));
    int paramType = AiParamGetType(paramEntry);

    if (nentry == NULL || paramEntry == NULL) {
        std::string msg = "Couldn't find attribute ";
        msg += arnoldName;
        msg += " from node ";
        msg += AiNodeGetName(node);
        AiMsgWarning(msg.c_str());
        return;
    }

    UsdShadeInput paramInput = shader.GetInput(TfToken(usdName.c_str()));
    if (paramInput) {
        SdfPathVector sourcePaths;

        // First check if there's a connection to this input attribute
        if (paramInput.HasConnectedSource() && paramInput.GetRawConnectedSourcePaths(&sourcePaths) &&
            !sourcePaths.empty()) {
            // just take the first target..., or should we check if the
            // attribute is an array ?
            context.addConnection(node, arnoldName, sourcePaths[0].GetPrimPath().GetText(), UsdArnoldReaderContext::CONNECTION_LINK);
        } else {
            // Just set the attribute value.
            // Switch depending on arnold attr type
            switch (paramType) {
                {
                    case AI_TYPE_BOOLEAN:
                        bool boolVal;
                        if (paramInput.Get(&boolVal))
                            AiNodeSetBool(node, arnoldName.c_str(), boolVal);
                        break;
                }
                {
                    case AI_TYPE_BYTE:
                        unsigned char charVal;
                        if (paramInput.Get(&charVal))
                            AiNodeSetByte(node, arnoldName.c_str(), charVal);
                        break;
                }
                {
                    case AI_TYPE_UINT:
                        unsigned int uintVal;
                        if (paramInput.Get(&uintVal))
                            AiNodeSetUInt(node, arnoldName.c_str(), uintVal);
                        break;
                }
                {
                    case AI_TYPE_INT:
                        int intVal;
                        if (paramInput.Get(&intVal))
                            AiNodeSetInt(node, arnoldName.c_str(), intVal);
                        break;
                }
                {
                    case AI_TYPE_FLOAT:
                        float fltVal;
                        if (paramInput.Get(&fltVal))
                            AiNodeSetFlt(node, arnoldName.c_str(), fltVal);
                        break;
                }
                {
                    case AI_TYPE_VECTOR2:
                        GfVec2f vec2Val;
                        if (paramInput.Get(&vec2Val))
                            AiNodeSetVec2(node, arnoldName.c_str(), vec2Val[0], vec2Val[1]);
                        break;
                }
                {
                    case AI_TYPE_VECTOR:
                        GfVec3f vecVal;
                        if (paramInput.Get(&vecVal))
                            AiNodeSetVec(node, arnoldName.c_str(), vecVal[0], vecVal[1], vecVal[2]);
                        break;
                }
                {
                    case AI_TYPE_RGB:
                        GfVec3f vecVal;
                        if (paramInput.Get(&vecVal))
                            AiNodeSetRGB(node, arnoldName.c_str(), vecVal[0], vecVal[1], vecVal[2]);
                        break;
                }
                {
                    case AI_TYPE_RGBA:
                        GfVec4f rgbaVal;
                        if (paramInput.Get(&rgbaVal))
                            AiNodeSetRGBA(node, arnoldName.c_str(), rgbaVal[0], rgbaVal[1], rgbaVal[2], rgbaVal[3]);
                        break;
                }
                {
                    case AI_TYPE_STRING:
                        TfToken tokenVal;
                        if (paramInput.Get(&tokenVal)) {
                            AiNodeSetStr(node, arnoldName.c_str(), tokenVal.GetText());
                        } else {
                            // "Asset"  parameters (for filenames) won't work
                            // with TfToken, let's try again with a SdfAssetPath
                            SdfAssetPath assetPath;
                            if (paramInput.Get(&assetPath)) {
                                // Should we use the resolved path here ? I'm
                                // doing it because Arnold might not know the
                                // usd "search" paths. This happens during the
                                // usd_procedural expansion, so it shouldn't be
                                // a problem.
                                AiNodeSetStr(node, arnoldName.c_str(), assetPath.GetResolvedPath().c_str());
                            }
                        }
                        break;
                }
                default:
                    // Arrays not supported yet
                    break;
            }
        }
    }
}

