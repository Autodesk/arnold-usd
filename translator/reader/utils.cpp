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

#include <constant_strings.h>

#include "../arnold_usd.h"
#include "reader.h"

#if PXR_VERSION >= 2002
#include <pxr/usd/usdShade/materialBindingAPI.h>
#endif

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

static inline void getMatrix(const UsdPrim &prim, AtMatrix &matrix, float frame, 
    UsdArnoldReaderContext &context, bool isXformable = true)
{
    GfMatrix4d xform;
    UsdGeomXformCache *xformCache = context.GetXformCache(frame);

    bool createXformCache = (xformCache == nullptr);
    if (createXformCache)
        xformCache = new UsdGeomXformCache(frame);

    // Special case for arnold schemas. They're not yet recognized as UsdGeomXformables, 
    // so we can't get their local to world transform. In that case, we ask for its parent
    // and we manually apply the local matrix on top of it
    if (isXformable){
        context.GetReader()->GetWorldMatrix(prim, xformCache, xform);
    }
    else {
        context.GetReader()->GetWorldMatrix(prim.GetParent(), xformCache, xform);
        UsdGeomXformable xformable(prim);
        GfMatrix4d localTransform;
        bool resetStack = true;
        if (xformable.GetLocalTransformation(&localTransform, &resetStack, UsdTimeCode(frame)))
        {
            xform *= localTransform;
        }
    }

    if (createXformCache)
        delete xformCache;

    const double *array = xform.GetArray();
    for (unsigned int i = 0; i < 4; ++i)
        for (unsigned int j = 0; j < 4; ++j)
            matrix[i][j] = array[4 * i + j];
}
/** Read Xformable transform as an arnold shape "matrix"
 */
void ReadMatrix(const UsdPrim &prim, AtNode *node, const TimeSettings &time, 
                    UsdArnoldReaderContext &context, bool isXformable)
{
    AtArray *matrices = context.GetMatrices();
    if (matrices) {
        // need to copy the array, as it will be deleted by 
        // UsdArnoldReaderContext's destructor after this primitive is translated
        AiNodeSetArray(node, str::matrix, AiArrayCopy(matrices));
    } else {
        matrices = ReadMatrix(prim, time, context, isXformable);
        AiNodeSetArray(node, str::matrix, matrices);
    }
    // If the matrices have multiple keys, it means that we have motion blur
    // and that we should set the motion_start / motion_end 
    if (AiArrayGetNumKeys(matrices) > 1) {
        AiNodeSetFlt(node, str::motion_start, time.motionStart);
        AiNodeSetFlt(node, str::motion_end, time.motionEnd);
    }
}
AtArray *ReadMatrix(const UsdPrim &prim, const TimeSettings &time, UsdArnoldReaderContext &context, bool isXformable)
{
    UsdGeomXformable xformable(prim);
    bool animated = xformable.TransformMightBeTimeVarying();
    if (time.motionBlur && !animated) {
        UsdPrim parent = prim.GetParent();
        while(parent) {
            UsdGeomXformable parentXform(parent);
            if (parentXform && parentXform.TransformMightBeTimeVarying()) {
                animated = true;
                break;
            }
            parent = parent.GetParent();
        }
    }
    AtMatrix matrix;
    AtArray *array = nullptr;
    if (time.motionBlur && animated) {
        // animated matrix, need to make it an array
        GfInterval interval(time.start(), time.end(), false, false);
        std::vector<double> timeSamples;
        xformable.GetTimeSamplesInInterval(interval, &timeSamples);
        // need to add the start end end keys (interval has open bounds)
        size_t numKeys = timeSamples.size() + 2;
        array = AiArrayAllocate(1, numKeys, AI_TYPE_MATRIX);
        float timeStep = float(interval.GetMax() - interval.GetMin()) / int(numKeys - 1);
        float timeVal = interval.GetMin();
        for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
            getMatrix(prim, matrix, timeVal, context, isXformable);
            AiArraySetMtx(array, i, matrix);
        }
    } else {
        // no motion, we just need a single matrix
        getMatrix(prim, matrix, time.frame, context, isXformable);
        array = AiArrayConvert(1, 1, AI_TYPE_MATRIX, &matrix);
    }
    return array;
}

AtArray *ReadLocalMatrix(const UsdPrim &prim, const TimeSettings &time)
{
    UsdGeomXformable xformable(prim);
    bool animated = xformable.TransformMightBeTimeVarying();
    
    AtMatrix matrix;
    AtArray *array = nullptr;

    auto ConvertAtMatrix = [](UsdGeomXformable &xformable, AtMatrix &m, float frame)
    { 
        GfMatrix4d localTransform;
        bool resetStack = true;
        if (xformable.GetLocalTransformation(&localTransform, &resetStack, UsdTimeCode(frame))) {
            const double *array = localTransform.GetArray();
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j, array++)
                    m[i][j] = (float)*array;

            return true;
        }
        return false;
    };
    if (time.motionBlur && animated) {
        // animated matrix, need to make it an array
        GfInterval interval(time.start(), time.end(), false, false);
        std::vector<double> timeSamples;
        xformable.GetTimeSamplesInInterval(interval, &timeSamples);
        // need to add the start end end keys (interval has open bounds)
        size_t numKeys = timeSamples.size() + 2;
        array = AiArrayAllocate(1, numKeys, AI_TYPE_MATRIX);
        float timeStep = float(interval.GetMax() - interval.GetMin()) / int(numKeys - 1);
        float timeVal = interval.GetMin();
        for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
            if (ConvertAtMatrix(xformable, matrix, timeVal)) {
                AiArraySetMtx(array, i, matrix);    
            }
        }
    } else {
        // no motion, we just need a single matrix
        if (ConvertAtMatrix(xformable, matrix, time.frame)) {
            array = AiArrayConvert(1, 1, AI_TYPE_MATRIX, &matrix);
        }        
    }
    return array;

}

static void getMaterialTargets(const UsdPrim &prim, std::string &shaderStr, std::string *dispStr = nullptr)
{
#if PXR_VERSION >= 2002
    // We want to get the material assignment for the "full" purpose, which is meant for rendering
    UsdShadeMaterial mat = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial(UsdShadeTokens->full);
#else
    UsdShadeMaterial mat = UsdShadeMaterial::GetBoundMaterial(prim);
#endif

    if (!mat) {
        return;
    }
    // First search the material attachment in the arnold scope
    UsdShadeShader surface = mat.ComputeSurfaceSource(str::t_arnold);
    
    if (!surface) {
        surface = mat.ComputeSurfaceSource(str::t_mtlx);
    }
    if (!surface) {// not found, search in the global scope
        surface = mat.ComputeSurfaceSource();
    }

    if (surface) {
        // Found a surface shader, let's add a connection to it (to be processed later)
        shaderStr = surface.GetPath().GetText();
    } else {
        // No surface found in USD primitives

        // We have a single "shader" binding in arnold, whereas USD has "surface"
        // and "volume" For now we export volume only if surface is empty.
        UsdShadeShader volume = mat.ComputeVolumeSource(str::t_arnold);
        if (!volume)
            volume = mat.ComputeVolumeSource();

        if (volume)
            shaderStr = volume.GetPath().GetText();
    }

    if (dispStr) {
        UsdShadeShader displacement = mat.ComputeDisplacementSource(str::t_arnold);
        if (!displacement)
            displacement = mat.ComputeDisplacementSource();

        if (displacement) {
            // Check what shader is assigned for displacement. If it's a UsdPreviewSurface, 
            // which has a displacement output, we can't let it be translated as a standard_surface,
            // otherwise arnold will complain about the shader output being a closure.
            // In that case, we need to consider the shader attribute "displacement" and propagate
            // the connection to this attribute as the mesh disp_map
            TfToken id;
            displacement.GetIdAttr().Get(&id);
            if (id == str::t_UsdPreviewSurface) {
                // Get the shader attribute "displacement" and check if anything is connected to it
                // If nothing is connected, we don't want any displacement for this material
                UsdShadeInput dispInput = displacement.GetInput(str::t_displacement);
                SdfPathVector dispPaths;
                if (dispInput && dispInput.HasConnectedSource() && 
                    dispInput.GetRawConnectedSourcePaths(&dispPaths) && !dispPaths.empty()) {
                    *dispStr =  dispPaths[0].GetPrimPath().GetText();
                }
                return;
            }
            *dispStr = displacement.GetPath().GetText();
        }
    }
}

// Read the materials / shaders assigned to a shape (node)
void ReadMaterialBinding(const UsdPrim &prim, AtNode *node, UsdArnoldReaderContext &context, bool assignDefault)
{
    std::string shaderStr;
    std::string dispStr;
    bool isPolymesh = AiNodeIs(node, str::polymesh);

    getMaterialTargets(prim, shaderStr, isPolymesh ? &dispStr : nullptr);

    if (!shaderStr.empty()) {
        context.AddConnection(node, "shader", shaderStr, UsdArnoldReader::CONNECTION_PTR);
    } else if (assignDefault) {
        AiNodeSetPtr(node, str::shader, context.GetReader()->GetDefaultShader());
    }

    if (isPolymesh && !dispStr.empty()) {
        context.AddConnection(node, "disp_map", dispStr, UsdArnoldReader::CONNECTION_PTR);
    }
}

// Read the materials / shaders assigned to geometry subsets, e.g. with per-face shader assignments
void ReadSubsetsMaterialBinding(
    const UsdPrim &prim, AtNode *node, UsdArnoldReaderContext &context, std::vector<UsdGeomSubset> &subsets,
    unsigned int elementCount, bool assignDefault)
{
    // We need to serialize the array of shaders in a string.
    std::string shadersArrayStr;
    std::string dispArrayStr;

    bool isPolymesh = AiNodeIs(node, str::polymesh);
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
        subset.GetIndicesAttr().Get(&subsetIndices, context.GetTimeSettings().frame);
        // Set the "shidxs" array with the indices for this subset
        for (size_t i = 0; i < subsetIndices.size(); ++i) {
            int idx = subsetIndices[i];
            if (idx < (int) elementCount)
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
        context.AddConnection(node, "shader", shadersArrayStr, UsdArnoldReader::CONNECTION_ARRAY);
    }
    if (hasDisplacement) {
        context.AddConnection(node, "disp_map", dispArrayStr, UsdArnoldReader::CONNECTION_ARRAY);
    }
    AtArray *shidxsArray = AiArrayConvert(elementCount, 1, AI_TYPE_BYTE, &(shidxs[0]));
    AiNodeSetArray(node, str::shidxs, shidxsArray);
}

// To find if a primitive is visible we need to call UsdGeomImageble::ComputeVisibility
// that will look for the visibility of all imageable primitives up in the scene hierarchy.
// But when we're loading a usd file with a given object_path we only traverse the scene
// starting at a given primitive and downwards, and therefore we need to ignore all visibility
// statements in the root's parents (see #1104)
bool IsPrimVisible(const UsdPrim &prim, UsdArnoldReader *reader, float frame)
{   
    UsdGeomImageable imageable = UsdGeomImageable(prim);
    if (!reader->HasRootPrim()) {
        // if there is no root prim in the reader, we just call ComputeVisibility
        return imageable ? (imageable.ComputeVisibility(frame) != UsdGeomTokens->invisible) : true;
    }
    // We have a root primitive: we need to compute the visibility only up to the root prim.
    // Since there is no USD API to do this, we need to compute it ourselves, by checking recursively
    // the visibility of each primitive, until we reach the root.

    if (prim == reader->GetRootPrim()) {
        // We reached the root primitive. We just want to check its own visibility without 
        // accounting for its eventual parents
        VtValue value;
        if (imageable && imageable.GetVisibilityAttr().Get(&value, frame))
            return value.Get<TfToken>() != UsdGeomTokens->invisible;
        return true;
    }
    // We didn't reach the root primitive yet, we're calling this function recursively, 
    // so that we can call the ComputeVisibility function with a parent visibility argument
    // (this function doesn't look for parent and just uses the input we give it).
    UsdPrim parent = prim.GetParent();
    if (!parent)
        return true;
    
    bool parentVisibility = IsPrimVisible(parent, reader, frame);
    if (!parentVisibility)
        return false;
    
    if (!imageable)
        return true;

    VtValue value;
    if (imageable.GetVisibilityAttr().Get(&value, frame)) {
        return value.Get<TfToken>() != UsdGeomTokens->invisible;
    }
    return true;
}
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

bool PrimvarsRemapper::RemapValues(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
        VtValue &value)
{
    return false;
}

bool PrimvarsRemapper::RemapIndexes(const UsdGeomPrimvar &primvar, const TfToken &interpolation, 
        std::vector<unsigned int> &indexes)
{
    return false;
}

void PrimvarsRemapper::RemapPrimvar(TfToken &name, std::string &interpolation)
{
}

