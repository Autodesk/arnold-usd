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

#include "reader.h"

#if PXR_VERSION >= 2002
#include <pxr/usd/usdShade/materialBindingAPI.h>
#endif

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (ArnoldNodeGraph)
    ((PrimvarsArnoldFiltermap, "primvars:arnold:filtermap"))
    ((PrimvarsArnoldUvRemap, "primvars:arnold:uv_remap"))
);


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
    // First search the material attachment in the arnold scope, then in the mtlx one
    // Finally, ComputeSurfaceSource will look into the universal scope
#if PXR_VERSION >= 2108
    TfTokenVector contextList {str::t_arnold, str::t_mtlx};
    UsdShadeShader surface = mat.ComputeSurfaceSource(contextList);
#else
    // old method, we need to ask for each context explicitely
    // First search the material attachment in the arnold scope
    UsdShadeShader surface = mat.ComputeSurfaceSource(str::t_arnold);
    if (!surface) { // not found, check in the mtlx scope
        surface = mat.ComputeSurfaceSource(str::t_mtlx);
    }
    if (!surface) {// not found, search in the global scope
        surface = mat.ComputeSurfaceSource();
    }

#endif
    
    if (surface) {
        // Found a surface shader, let's add a connection to it (to be processed later)
        shaderStr = surface.GetPath().GetText();
    } else {
        // No surface found in USD primitives

        // We have a single "shader" binding in arnold, whereas USD has "surface"
        // and "volume" For now we export volume only if surface is empty.
#if PXR_VERSION >= 2108
        UsdShadeShader volume = mat.ComputeVolumeSource(contextList);
#else
        // old method, we need to ask for each context explicitely
        UsdShadeShader volume = mat.ComputeVolumeSource(str::t_arnold);
        if (!volume)
            volume = mat.ComputeVolumeSource();
#endif

        if (volume)
            shaderStr = volume.GetPath().GetText();
    }

    if (dispStr) { 
        // first check displacement in the arnold scope, then in the mtlx one,
        // finally, ComputeDisplacementSource will look into the universal scope
#if PXR_VERSION >= 2108
        UsdShadeShader displacement = mat.ComputeDisplacementSource(contextList);
#else
        // old method, we need to ask for each context explicitely.
        // First check displacement in the arnold scope
        UsdShadeShader displacement = mat.ComputeDisplacementSource(str::t_arnold);
        if (!displacement) { // not found, search in the mtlx scope
            displacement = mat.ComputeDisplacementSource(str::t_mtlx);
        }
        if (!displacement) { // still not found, search in the global scope
            displacement = mat.ComputeDisplacementSource();
        }
#endif        
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

    // When prototypeName is not empty, but we are reading inside the prototype of a SkelRoot and not the actual 
    // instanced prim. The material should be bound on the instanced prim, so we look for it in the stage.
    UsdPrim materialBoundPrim(prim);
    if (!context.GetPrototypeName().empty()) {
        SdfPath pathConsidered(context.GetArnoldNodeName(prim.GetPath().GetText()));
        materialBoundPrim = prim.GetStage()->GetPrimAtPath(pathConsidered);
    }
    getMaterialTargets(materialBoundPrim, shaderStr, isPolymesh ? &dispStr : nullptr);

    if (!shaderStr.empty()) {
        context.AddConnection(node, "shader", shaderStr, ArnoldAPIAdapter::CONNECTION_PTR);
    } else if (assignDefault) {
        AiNodeSetPtr(node, str::shader, context.GetReader()->GetDefaultShader());
    }

    if (isPolymesh && !dispStr.empty()) {
        context.AddConnection(node, "disp_map", dispStr, ArnoldAPIAdapter::CONNECTION_PTR);
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
        context.AddConnection(node, "shader", shadersArrayStr, UsdArnoldReaderThreadContext::CONNECTION_ARRAY);
    }
    if (hasDisplacement) {
        context.AddConnection(node, "disp_map", dispArrayStr, UsdArnoldReaderThreadContext::CONNECTION_ARRAY);
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


size_t ReadTopology(UsdAttribute& usdAttr, AtNode* node, const char* attrName, const TimeSettings& time, UsdArnoldReaderContext &context)
{
    uint8_t attrType = AI_TYPE_VECTOR;
    bool animated = time.motionBlur && usdAttr.ValueMightBeTimeVarying();
    UsdArnoldSkelData *skelData = context.GetSkelData();
 
    const std::vector<UsdTimeCode> *skelTimes = (skelData) ? &(skelData->GetTimes()) : nullptr;
    if (skelTimes && skelTimes->size() > 1)
        animated = true;
        // check if skinning is animated

    if (!animated) {
        // Single-key arrays
        VtValue val;
        if (!usdAttr.Get(&val, time.frame))
            return 0;

        const VtArray<GfVec3f>& array = val.Get<VtArray<GfVec3f>>();
        if (array.size() > 0) {
            // First test if this mesh is skinned
            VtArray<GfVec3f> skinnedArray;
            if (skelData && skelData->ApplyPointsSkinning(usdAttr.GetPrim(), array, skinnedArray, context, time.frame, UsdArnoldSkelData::SKIN_POINTS)) {
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(skinnedArray.size(), 1, attrType, skinnedArray.cdata()));
            } else {
                AiNodeSetArray(node, AtString(attrName), AiArrayConvert(array.size(), 1, attrType, array.cdata()));
            }
        } else
            AiNodeResetParameter(node, AtString(attrName));

        return 1; // return the amount of keys
    } else {
        // Animated array
        GfInterval interval(time.start(), time.end(), false, false);
        size_t numKeys = 0;

        if (skelTimes && !skelTimes->empty()) {
            numKeys = skelTimes->size() - 1;

        } else {
            std::vector<double> timeSamples;
            usdAttr.GetTimeSamplesInInterval(interval, &timeSamples);
            numKeys = timeSamples.size();
            // need to add the start end end keys (interval has open bounds)
            numKeys += 2;
        }
        
        float timeStep = float(interval.GetMax() - interval.GetMin()) / int(numKeys - 1);
        float timeVal = interval.GetMin();

        VtValue val;
        if (!usdAttr.Get(&val, timeVal))
            return 0;

        const VtArray<GfVec3f>* array = &(val.Get<VtArray<GfVec3f>>());
        VtArray<GfVec3f> skinnedArray;

        // Arnold arrays don't support varying element counts per key.
        // So if we find that the size changes over time, we will just take a single key for the current frame        
        size_t size = array->size();
        if (size == 0)
            return 0;        
        
        GfVec3f* arnoldVec = new GfVec3f[size * numKeys], *ptr = arnoldVec;
        for (size_t i = 0; i < numKeys; i++, timeVal += timeStep) {
            if (i > 0) {
                // if a time sample is missing, we can't translate 
                // this attribute properly
                if (!usdAttr.Get(&val, timeVal)) {
                    size = 0;
                    break;
                }
                array = &(val.Get<VtArray<GfVec3f>>());
            }            
            if (array->size() != size) {
                 // Arnold won't support varying element count. 
                // We need to only consider a single key corresponding to the current frame
                if (!usdAttr.Get(&val, time.frame)) {
                    size = 0;
                    break;
                }                        

                delete [] arnoldVec;
                array = &(val.Get<VtArray<GfVec3f>>()); 

                size = array->size(); // update size to the current frame one
                numKeys = 1; // we just want a single key now
                // reallocate the array
                arnoldVec = new GfVec3f[size * numKeys];
                ptr = arnoldVec;
                i = numKeys; // this will stop the "for" loop after the concatenation
                
            }
            if (skelData && skelData->ApplyPointsSkinning(usdAttr.GetPrim(), *array, skinnedArray, context, timeVal, UsdArnoldSkelData::SKIN_POINTS)) {
                array = &skinnedArray;
            }
            // The array can be null, and the number of element in AiNodeSetArray will be incorrect
            for (unsigned j=0; j < array->size(); j++)
                *ptr++ = array->data()[j];
        }

        if (size > 0) {
            AiNodeSetArray(node, AtString(attrName), AiArrayConvert(size, numKeys, attrType, arnoldVec));
        }
        else
            numKeys = 0;

        delete [] arnoldVec;
        return numKeys;
    }
}

void ApplyParentMatrices(AtArray *matrices, const AtArray *parentMatrices)
{
    if (matrices == nullptr || parentMatrices == nullptr)
        return;
    
    unsigned int matrixNumKeys = AiArrayGetNumKeys(matrices);
    unsigned int parentMatrixNumKeys = AiArrayGetNumKeys(parentMatrices);

    if (matrixNumKeys == 0 || parentMatrixNumKeys == 0)
        return;

    bool interpolate = (matrixNumKeys != parentMatrixNumKeys);
    for (unsigned int i = 0; i < matrixNumKeys; ++i) {
        if (interpolate) {
            AtMatrix m = AiM4Mult(AiArrayGetMtx(matrices, i), AiArrayInterpolateMtx(parentMatrices, (float)i / AiMax(float(parentMatrixNumKeys - 1), 1.f), 0));
            AiArraySetMtx(matrices, i, m);

        } else {
            AtMatrix m = AiM4Mult(AiArrayGetMtx(matrices, i), AiArrayGetMtx(parentMatrices, i));
            AiArraySetMtx(matrices, i, m);
        }
    }
}

bool ReadNodeGraphAttr(const UsdPrim &prim, AtNode *node, const UsdAttribute &attr, 
                                    const std::string &attrName, UsdArnoldReaderContext &context,
                                    UsdArnoldReaderContext::ConnectionType cType) {


    bool success = false;
    // Read eventual connections to a ArnoldNodeGraph primitive, that acts as a passthrough
    const TimeSettings &time = context.GetTimeSettings();
    VtValue value;
    if (attr && attr.Get(&value, time.frame)) {
        // RenderSettings have a string attribute, referencing a prim in the stage
        std::string valStr;
        // First check if the attribute is actually holding a "string" value
        if (value.IsHolding<std::string>() || value.IsHolding<TfToken>() ||  value.IsHolding<SdfPath>())
            valStr = VtValueGetString(value, &attr);
        
        if (!valStr.empty()) {
            SdfPath path(valStr);
            // We check if there is a primitive at the path of this string
            UsdPrim ngPrim = context.GetReader()->GetStage()->GetPrimAtPath(SdfPath(valStr));
            // We verify if the primitive is indeed a ArnoldNodeGraph
            if (ngPrim && ngPrim.GetTypeName() == _tokens->ArnoldNodeGraph) {
                // We can use a UsdShadeShader schema in order to read connections
                UsdShadeShader ngShader(ngPrim);
                
                bool isArray = false;
                if (cType == ArnoldAPIAdapter::CONNECTION_ARRAY) {
                    isArray = true;
                    cType = ArnoldAPIAdapter::CONNECTION_PTR;
                }
                int arrayIndex = 0;
                while(true) {

                    std::string outAttrName = attrName;
                    std::string connAttrName = attrName;
                    if (isArray) {
                        // usd format for arrays
                        std::string idStr = std::to_string(++arrayIndex);
                        outAttrName += std::string(":i") + idStr;
                        // format used internally in the reader to recognize arrays easily
                        connAttrName += std::string("[") + idStr + std::string("]");

                    }
                    
                    // the output attribute must have the same name as the input one in the RenderSettings
                    UsdShadeOutput outputAttr = ngShader.GetOutput(TfToken(outAttrName));
                    if (outputAttr) {
                        SdfPathVector sourcePaths;
                        // Check which shader is connected to this output
                        if (outputAttr.HasConnectedSource() && outputAttr.GetRawConnectedSourcePaths(&sourcePaths) &&
                            !sourcePaths.empty()) {
                            SdfPath outPath(sourcePaths[0].GetPrimPath());
                            UsdPrim outPrim = context.GetReader()->GetStage()->GetPrimAtPath(outPath);
                            if (outPrim) {
                                context.AddConnection(node, connAttrName, outPath.GetText(), cType);
                            }
                        }
                        success = true;
                    } else
                        break;
                    if (!isArray)
                        break;
                }
            }
        }

    }
    return success;
}


void ReadLightShaders(const UsdPrim& prim, const UsdAttribute &shadersAttr, AtNode *node, UsdArnoldReaderContext &context)
{    
    if (!shadersAttr || !shadersAttr.HasAuthoredValue()) {
        return;
    }
    
    ReadNodeGraphAttr(prim, node, shadersAttr, "color", context, ArnoldAPIAdapter::CONNECTION_LINK);
    ReadNodeGraphAttr(prim, node, shadersAttr, "filters", context, ArnoldAPIAdapter::CONNECTION_ARRAY);
}

void ReadCameraShaders(const UsdPrim& prim, AtNode *node, UsdArnoldReaderContext &context)
{   
    UsdAttribute filtermapAttr = prim.GetAttribute(_tokens->PrimvarsArnoldFiltermap); 
    if (filtermapAttr && filtermapAttr.HasAuthoredValue()) {
        ReadNodeGraphAttr(prim, node, filtermapAttr, "filtermap", context, ArnoldAPIAdapter::CONNECTION_PTR);    
    }
    UsdAttribute uvRemapAttr = prim.GetAttribute(_tokens->PrimvarsArnoldUvRemap);
    if (uvRemapAttr && uvRemapAttr.HasAuthoredValue()) {
        ReadNodeGraphAttr(prim, node, uvRemapAttr, "uv_remap", context, ArnoldAPIAdapter::CONNECTION_LINK);
    }
}

int GetTimeSampleNumKeys(const UsdPrim &prim, const TimeSettings &time, TfToken interpolation) {
    int numKeys = 2;
    if (UsdAttribute deformKeysAttr = prim.GetAttribute(TfToken("primvars:arnold:deform_keys"))) {
        UsdGeomPrimvar primvar(deformKeysAttr);
        if (primvar && primvar.GetInterpolation() == interpolation) {
            int deformKeys = 0;
            if (deformKeysAttr.Get(&deformKeys, UsdTimeCode(time.frame))) {
                numKeys = deformKeys > 0 ? deformKeys : 1;
            }
        }
    }
    return numKeys;
}