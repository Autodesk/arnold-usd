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

#include "../arnold_usd.h"
#include "reader.h"

#if USED_USD_VERSION_GREATER_EQ(20, 2)
#include <pxr/usd/usdShade/materialBindingAPI.h>
#endif

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

static inline void getMatrix(const UsdPrim &prim, AtMatrix &matrix, float frame, UsdArnoldReaderContext &context)
{
    GfMatrix4d xform;
    bool dummyBool = false;
    UsdGeomXformCache *xformCache = context.GetXformCache(frame);

    bool createXformCache = (xformCache == nullptr);
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
void ExportMatrix(const UsdPrim &prim, AtNode *node, const TimeSettings &time, UsdArnoldReaderContext &context)
{
    UsdGeomXformable xformable(prim);
    bool animated = xformable.TransformMightBeTimeVarying();
    AtMatrix matrix;
    if (time.motionBlur && animated) {
        // animated matrix, need to make it an array
        GfInterval interval(time.start(), time.end(), false, false);
        std::vector<double> timeSamples;
        xformable.GetTimeSamplesInInterval(interval, &timeSamples);
        // need to add the start end end keys (interval has open bounds)
        size_t numKeys = timeSamples.size() + 2;
        AtArray *array = AiArrayAllocate(1, numKeys, AI_TYPE_MATRIX);
        float timeStep = float(interval.GetMax() - interval.GetMin()) / int(numKeys - 1);
        float timeVal = interval.GetMin();
        for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
            getMatrix(prim, matrix, timeVal, context);
            AiArraySetMtx(array, i, matrix);
        }
        AiNodeSetArray(node, "matrix", array);
        AiNodeSetFlt(node, "motion_start", time.motionStart);
        AiNodeSetFlt(node, "motion_end", time.motionEnd);
    } else {
        getMatrix(prim, matrix, time.frame, context);
        // set the attribute
        AiNodeSetMatrix(node, "matrix", matrix);
    }
}

static void getMaterialTargets(const UsdPrim &prim, std::string &shaderStr, std::string *dispStr = nullptr)
{
#if USED_USD_VERSION_GREATER_EQ(20, 2)
    UsdShadeMaterial mat = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
#else
    UsdShadeMaterial mat = UsdShadeMaterial::GetBoundMaterial(prim);
#endif

    if (!mat) {
        return;
    }
    TfToken arnoldContext("arnold");
    // First search the material attachment in the arnold scope
    UsdShadeShader surface = mat.ComputeSurfaceSource(arnoldContext);
    if (!surface) // not found, search in the global scope
        surface = mat.ComputeSurfaceSource();

    if (surface) {
        // Found a surface shader, let's add a connection to it (to be processed later)
        shaderStr = surface.GetPath().GetText();
    } else {
        // No surface found in USD primitives

        // We have a single "shader" binding in arnold, whereas USD has "surface"
        // and "volume" For now we export volume only if surface is empty.
        UsdShadeShader volume = mat.ComputeVolumeSource(arnoldContext);
        if (!volume)
            volume = mat.ComputeVolumeSource();

        if (volume)
            shaderStr = volume.GetPath().GetText();
    }

    if (dispStr) {
        UsdShadeShader displacement = mat.ComputeDisplacementSource(arnoldContext);
        if (!displacement)
            displacement = mat.ComputeDisplacementSource();

        if (displacement)
            *dispStr = displacement.GetPath().GetText();
    }
}

// Export the materials / shaders assigned to a shape (node)
void ExportMaterialBinding(const UsdPrim &prim, AtNode *node, UsdArnoldReaderContext &context, bool assignDefault)
{
    std::string shaderStr;
    std::string dispStr;
    static const AtString polymeshStr("polymesh");
    bool isPolymesh = AiNodeIs(node, polymeshStr);

    getMaterialTargets(prim, shaderStr, isPolymesh ? &dispStr : nullptr);

    if (!shaderStr.empty()) {
        context.AddConnection(node, "shader", shaderStr, UsdArnoldReaderContext::CONNECTION_PTR);
    } else if (assignDefault) {
        AiNodeSetPtr(node, "shader", context.GetReader()->GetDefaultShader());
    }

    if (isPolymesh && !dispStr.empty()) {
        context.AddConnection(node, "disp_map", dispStr, UsdArnoldReaderContext::CONNECTION_PTR);
    }
}

// Export the materials / shaders assigned to geometry subsets, e.g. with per-face shader assignments
void ExportSubsetsMaterialBinding(
    const UsdPrim &prim, AtNode *node, UsdArnoldReaderContext &context, std::vector<UsdGeomSubset> &subsets,
    unsigned int elementCount, bool assignDefault)
{
    // We need to serialize the array of shaders in a string.
    std::string shadersArrayStr;
    std::string dispArrayStr;

    static const AtString polymeshStr("polymesh");
    bool isPolymesh = AiNodeIs(node, polymeshStr);
    bool hasDisplacement = false;

    std::string shaderStr;
    std::string dispStr;

    // If some faces aren't assigned to any geom subset, we'll add a shader to the list.
    // So by default we're assigning a shader index that equals the amount of subsets.
    // If, after dealing with all the subsets, we still have indices equal to this value,
    // we will need to add a shader to the list.
    unsigned char unassignedIndex = (unsigned char)subsets.size();
    std::vector<unsigned char> shidxs(elementCount, unassignedIndex);
    int shidx = 0;

    for (auto subset : subsets) {
        shaderStr.clear();
        dispStr.clear();

        getMaterialTargets(subset.GetPrim(), shaderStr, isPolymesh ? &dispStr : nullptr);
        if (shaderStr.empty() && assignDefault) {
            shaderStr = AiNodeGetName(context.GetReader()->GetDefaultShader());
        }
        if (shaderStr.empty())
            shaderStr = "NULL";

        if (shidx > 0)
            shadersArrayStr += " ";

        shadersArrayStr += shaderStr;

        // For polymeshes, check if there is some displacement for this subset
        if (isPolymesh) {
            if (dispStr.empty())
                dispStr = "NULL";
            else
                hasDisplacement = true;

            if (shidx > 0)
                dispArrayStr += " ";
            dispArrayStr += dispStr;
        }
        VtIntArray subsetIndices;
        subset.GetIndicesAttr().Get(&subsetIndices);
        // Set the "shidxs" array with the indices for this subset
        for (size_t i = 0; i < subsetIndices.size(); ++i) {
            int idx = subsetIndices[i];
            if (idx < elementCount)
                shidxs[idx] = shidx;
        }
        shidx++;
    }
    bool needUnassignedShader = false;
    // Verify if some faces weren't part of any subset.
    // If so, we need to create a new shader
    for (auto shidxElem : shidxs) {
        if (shidxElem == unassignedIndex) {
            needUnassignedShader = true;
            break;
        }
    }
    if (needUnassignedShader) {
        // For the "default" shader, we check the shader assigned to the geometry
        // primitive itself.

        shaderStr.clear();
        dispStr.clear();
        getMaterialTargets(prim, shaderStr, isPolymesh ? &dispStr : nullptr);
        if (shaderStr.empty() && assignDefault) {
            shaderStr = AiNodeGetName(context.GetReader()->GetDefaultShader());
        }
        if (shaderStr.empty())
            shaderStr = "NULL";

        shadersArrayStr += " ";
        shadersArrayStr += shaderStr;
        if (isPolymesh) {
            if (dispStr.empty())
                dispStr = "NULL";
            else
                hasDisplacement = true;

            dispArrayStr += " ";
            dispArrayStr += dispStr;
        }
    }

    // Set the shaders array, for the array connections to be applied later
    if (!shadersArrayStr.empty()) {
        context.AddConnection(node, "shader", shadersArrayStr, UsdArnoldReaderContext::CONNECTION_ARRAY);
    }
    if (hasDisplacement) {
        context.AddConnection(node, "disp_map", dispArrayStr, UsdArnoldReaderContext::CONNECTION_ARRAY);
    }
    AtArray *shidxsArray = AiArrayConvert(elementCount, 1, AI_TYPE_BYTE, &(shidxs[0]));
    AiNodeSetArray(node, "shidxs", shidxsArray);
}

/**
 * Export a specific shader parameter from USD to Arnold
 *
 **/

void ExportShaderParameter(
    UsdShadeShader &shader, AtNode *node, const std::string &usdName, const std::string &arnoldName,
    UsdArnoldReaderContext &context)
{
    if (node == nullptr)
        return;

    const AtNodeEntry *nentry = AiNodeGetNodeEntry(node);
    const AtParamEntry *paramEntry = AiNodeEntryLookUpParameter(nentry, AtString(arnoldName.c_str()));
    int paramType = AiParamGetType(paramEntry);

    if (nentry == nullptr || paramEntry == nullptr) {
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
            context.AddConnection(
                node, arnoldName, sourcePaths[0].GetPrimPath().GetText(), UsdArnoldReaderContext::CONNECTION_LINK);
        } else {
            // Just set the attribute value.
            // Switch depending on arnold attr type
            switch (paramType) {
                case AI_TYPE_BOOLEAN: {
                    bool boolVal;
                    if (paramInput.Get(&boolVal))
                        AiNodeSetBool(node, arnoldName.c_str(), boolVal);
                    break;
                }
                case AI_TYPE_BYTE: {
                    unsigned char charVal;
                    if (paramInput.Get(&charVal))
                        AiNodeSetByte(node, arnoldName.c_str(), charVal);
                    break;
                }
                case AI_TYPE_UINT: {
                    unsigned int uintVal;
                    if (paramInput.Get(&uintVal))
                        AiNodeSetUInt(node, arnoldName.c_str(), uintVal);
                    break;
                }
                case AI_TYPE_INT: {
                    int intVal;
                    if (paramInput.Get(&intVal))
                        AiNodeSetInt(node, arnoldName.c_str(), intVal);
                    break;
                }
                case AI_TYPE_FLOAT: {
                    float fltVal;
                    if (paramInput.Get(&fltVal))
                        AiNodeSetFlt(node, arnoldName.c_str(), fltVal);
                    break;
                }
                case AI_TYPE_VECTOR2: {
                    GfVec2f vec2Val;
                    if (paramInput.Get(&vec2Val))
                        AiNodeSetVec2(node, arnoldName.c_str(), vec2Val[0], vec2Val[1]);
                    break;
                }
                case AI_TYPE_VECTOR: {
                    GfVec3f vecVal;
                    if (paramInput.Get(&vecVal))
                        AiNodeSetVec(node, arnoldName.c_str(), vecVal[0], vecVal[1], vecVal[2]);
                    break;
                }
                case AI_TYPE_RGB: {
                    GfVec3f vecVal;
                    if (paramInput.Get(&vecVal))
                        AiNodeSetRGB(node, arnoldName.c_str(), vecVal[0], vecVal[1], vecVal[2]);
                    break;
                }
                case AI_TYPE_RGBA: {
                    GfVec4f rgbaVal;
                    if (paramInput.Get(&rgbaVal))
                        AiNodeSetRGBA(node, arnoldName.c_str(), rgbaVal[0], rgbaVal[1], rgbaVal[2], rgbaVal[3]);
                    break;
                }
                case AI_TYPE_STRING: {
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

size_t ExportStringArray(UsdAttribute attr, AtNode *node, const char *attrName, const TimeSettings &time)
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
        AiNodeSetArray(node, attrName, outArray);
    else
        AiNodeResetParameter(node, attrName);

    return size;
}