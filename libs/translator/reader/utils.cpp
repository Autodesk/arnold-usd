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
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include <constant_strings.h>
#include <parameters_utils.h>

#include "reader.h"

#include <pxr/usd/usdShade/materialBindingAPI.h>

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (ArnoldNodeGraph)
    ((PrimvarsArnoldFiltermap, "primvars:arnold:filtermap"))
    ((PrimvarsArnoldUvRemap, "primvars:arnold:uv_remap"))
    ((PrimvarsArnoldDeformKeys, "primvars:arnold:deform_keys"))
    ((PrimvarsArnoldTransformKeys, "primvars:arnold:transform_keys"))
);

bool HasConstantPrimvar(UsdArnoldReaderContext &context, const TfToken& name)
{
    const std::vector<UsdGeomPrimvar>& primvars = context.GetPrimvars();
    for (const auto& primvar : primvars) {
        if (primvar.GetName() == name)
            return true;
    }
    return false;
}

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
            xform = localTransform * xform;
        }
    }

    if (createXformCache)
        delete xformCache;

    ConvertValue(matrix, xform);
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
        if (matrices)
            AiNodeSetArray(node, str::matrix, matrices);
    }
    // If the matrices have multiple keys, it means that we have motion blur
    // and that we should set the motion_start / motion_end 
    if (matrices && AiArrayGetNumKeys(matrices) > 1) {
        AiNodeSetFlt(node, str::motion_start, time.motionStart);
        AiNodeSetFlt(node, str::motion_end, time.motionEnd);
    }
}
AtArray *ReadMatrix(const UsdPrim &prim, const TimeSettings &time, UsdArnoldReaderContext &context, bool isXformable)
{    
    // Shaders are primitives that don't need matrix checking, so we're doing an exception for it
    // since it's the most frequent prim type.
    // We can't check if the prim is a UsdGeomXformable, because some custom primitives
    // might actually expect a matrix even though USD doesn't know about it.
    if (prim.IsA<UsdShadeShader>())
        return nullptr;

    int numKeys = ComputeTransformNumKeys(prim, time, true);
    AtMatrix matrix;
    AtArray *array = nullptr;
    if (numKeys > 1) {
        GfInterval interval(time.start(), time.end(), false, false);
        array = AiArrayAllocate(1, numKeys, AI_TYPE_MATRIX);
        double timeStep = double(interval.GetMax() - interval.GetMin()) / int(numKeys - 1);
        double timeVal = interval.GetMin();
        for (int i = 0; i < numKeys; i++, timeVal += timeStep) {
            getMatrix(prim, matrix, static_cast<float>(timeVal), context, isXformable);
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
    AtMatrix matrix;
    AtArray *array = nullptr;

    auto ConvertAtMatrix = [](UsdGeomXformable &xformable, AtMatrix &m, float frame)
    { 
        GfMatrix4d localTransform;
        bool resetStack = true;
        if (xformable.GetLocalTransformation(&localTransform, &resetStack, UsdTimeCode(frame))) {
            ConvertValue(m, localTransform);
            return true;
        }
        return false;
    };

    int numKeys = ComputeTransformNumKeys(prim, time, false);
    if (numKeys > 1) {
        GfInterval interval(time.start(), time.end(), false, false);
        array = AiArrayAllocate(1, numKeys, AI_TYPE_MATRIX);
        double timeStep = double(interval.GetMax() - interval.GetMin()) / double(numKeys - 1);
        double timeVal = interval.GetMin();
        for (int i = 0; i < numKeys; i++, timeVal += timeStep) {
            if (ConvertAtMatrix(xformable, matrix, static_cast<float>(timeVal))) {
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
void GetMaterialTargets(const UsdShadeMaterial &mat, UsdPrim& shaderPrim, UsdPrim *dispPrim)
{
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
        shaderPrim = surface.GetPrim();
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
            shaderPrim = volume.GetPrim();
    }

    if (dispPrim) { 
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
                    *dispPrim = mat.GetPrim().GetStage()->GetPrimAtPath(dispPaths[0].GetPrimPath());
                }
                return;
            }
            *dispPrim = displacement.GetPrim();
        }
    }
}
static void _GetMaterialTargets(const UsdPrim &prim, UsdPrim& shaderPrim, UsdPrim *dispPrim = nullptr)
{
    // We want to get the material assignment for the "full" purpose, which is meant for rendering
    UsdShadeMaterial mat = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial(UsdShadeTokens->full);

    if (!mat) {
        return;
    }
    GetMaterialTargets(mat, shaderPrim, dispPrim);
}

// Read the materials / shaders assigned to a shape (node)
void ReadMaterialBinding(const UsdPrim &prim, AtNode *node, UsdArnoldReaderContext &context, bool assignDefault)
{
    bool isPolymesh = AiNodeIs(node, str::hdpolymesh);

    // When prototypeName is not empty, but we are reading inside the prototype of a SkelRoot and not the actual 
    // instanced prim. The material should be bound on the instanced prim, so we look for it in the stage.
    UsdPrim materialBoundPrim(prim);
    if (!context.GetPrototypeName().empty()) {
        SdfPath pathConsidered(context.GetArnoldNodeName(prim.GetPath().GetText()));
        materialBoundPrim = prim.GetStage()->GetPrimAtPath(pathConsidered);
    }
    UsdPrim shaderPrim, dispPrim;
    _GetMaterialTargets(materialBoundPrim, shaderPrim, isPolymesh ? &dispPrim : nullptr);

    if (shaderPrim) {
        context.AddConnection(node, "shader", shaderPrim.GetPath().GetString(), 
            ArnoldAPIAdapter::CONNECTION_PTR);
    } else if (assignDefault) {
        AiNodeSetPtr(node, str::shader, context.GetReader()->GetDefaultShader());
    }

    if (isPolymesh && dispPrim) {
        context.AddConnection(node, "disp_map", dispPrim.GetPath().GetString(), 
            ArnoldAPIAdapter::CONNECTION_PTR);
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

    bool isPolymesh = AiNodeIs(node, str::hdpolymesh);
    bool hasDisplacement = false;

    // If some faces aren't assigned to any geom subset, we'll add a shader to the list.
    // So by default we're assigning a shader index that equals the amount of subsets.
    // If, after dealing with all the subsets, we still have indices equal to this value,
    // we will need to add a shader to the list.
    unsigned char unassignedIndex = (unsigned char)subsets.size();
    std::vector<unsigned char> shidxs(elementCount, unassignedIndex);
    int shidx = 0;
    std::string shaderStr, dispStr;

    for (auto subset : subsets) {
        UsdPrim shaderPrim, dispPrim;
        shaderStr.clear();
        dispStr.clear();

        _GetMaterialTargets(subset.GetPrim(), shaderPrim, isPolymesh ? &dispPrim : nullptr);
        if (shaderPrim)
            shaderStr = shaderPrim.GetPath().GetString();
        else if (assignDefault) {
            shaderStr = AiNodeGetName(context.GetReader()->GetDefaultShader());
        }
        if (shaderStr.empty())
            shaderStr = "NULL";

        if (shidx > 0)
            shadersArrayStr += " ";

        shadersArrayStr += shaderStr;

        // For polymeshes, check if there is some displacement for this subset
        if (isPolymesh) {
            if (dispPrim) {
                dispStr = dispPrim.GetPath().GetString();
                hasDisplacement = true;
            } else {
                dispStr = "NULL";
            }
             
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
        UsdPrim shaderPrim, dispPrim;
        _GetMaterialTargets(prim, shaderPrim, isPolymesh ? &dispPrim : nullptr);

        if (shaderPrim) {
            shaderStr = shaderPrim.GetPath().GetString();
        } else if (assignDefault) {
            shaderStr = AiNodeGetName(context.GetReader()->GetDefaultShader());
        } else {
            shaderStr = "NULL";
        }

        shadersArrayStr += " ";
        shadersArrayStr += shaderStr;
        if (isPolymesh) {
            if (dispPrim) {
                dispStr = dispPrim.GetPath().GetString();
                hasDisplacement = true;
            } else {
                dispStr = "NULL";
            }               

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

        if (skelTimes && skelTimes->size() > 1) {
            numKeys = skelTimes->size();

        } else {
            numKeys = ComputeNumKeys(usdAttr, time);
        }
        
        double timeStep = double(interval.GetMax() - interval.GetMin()) / double(numKeys - 1);
        double timeVal = interval.GetMin();

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

    // If we need to reinterpolate, we want the final matrix to have the max number of keys
    if (matrixNumKeys == parentMatrixNumKeys) {
        for (unsigned int i = 0; i < matrixNumKeys; ++i) {
            AtMatrix m = AiM4Mult(AiArrayGetMtx(matrices, i), AiArrayInterpolateMtx(parentMatrices, (float)i / AiMax(float(parentMatrixNumKeys - 1), 1.f), 0));
            AiArraySetMtx(matrices, i, m);
        }
    } else if (matrixNumKeys >= parentMatrixNumKeys) {
        for (unsigned int i = 0; i < matrixNumKeys; ++i) {
            AtMatrix m = AiM4Mult(AiArrayGetMtx(matrices, i), AiArrayGetMtx(parentMatrices, i));
            AiArraySetMtx(matrices, i, m);
        }
    } else { // The number of matrices of the parent is greater than the child, it can happen on instances, we resize the current matrix
        AtArray *tmpMatrices = AiArrayCopy(matrices);
        AiArrayResize(matrices, 1, parentMatrixNumKeys);
        for (unsigned int i = 0; i < parentMatrixNumKeys; ++i) {
            AtMatrix m = AiM4Mult(AiArrayInterpolateMtx(tmpMatrices, (float)i / AiMax(float(parentMatrixNumKeys - 1), 1.f), 0), AiArrayGetMtx(parentMatrices, i));
            AiArraySetMtx(matrices, i, m);
        }
        AiArrayDestroy(tmpMatrices);
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
        std::string valStr = VtValueGetString(value);
        
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

// Return the number of keys needed by Arnold
int ComputeTransformNumKeys(const UsdPrim &prim, const TimeSettings &time, bool checkParents) {
    // No motion blur, need just 1 key
    if (!time.motionBlur) return 1;
    
    const auto getNumKeys = [&](const TfToken& attr) -> int {
        if (UsdAttribute numKeysAttr = prim.GetAttribute(attr)) {
            int numKeys = 0;
            if (numKeysAttr.Get(&numKeys, UsdTimeCode(time.frame)) && numKeys > 0)
                return numKeys;
        }
        return 0;
    };
    // Check if the attribute "transform_keys" is set in order to provide 
    // an explicit amount of keys in the arnold matrix array. If not present, 
    // we look for "deform_keys" (which is now deprecated for transforms)
    int numKeys = getNumKeys(_tokens->PrimvarsArnoldTransformKeys);
    if (numKeys == 0)
        numKeys = getNumKeys(_tokens->PrimvarsArnoldDeformKeys);

    if (numKeys > 0)
        return numKeys;

    // Since no explicit amount of keys was provided for this primitive, 
    // we compute it automatically based on the amount of samples found
    // in the shutter interval
    
    numKeys = 2; // We need at least 2 keys at the interval boundaries
    // If the prim is a transform we have a special logic
    UsdGeomXformable xformable(prim);
    if (xformable) {
        // This logic was originally coded in ReadMatrix and ReadLocalMatrix
        std::vector<double> timeSamples;

        // Find the first prim with animation
        UsdPrim primIt = prim;
        while (primIt) {
            UsdGeomXformable xform(primIt);
            if (xform && xform.TransformMightBeTimeVarying()) {
                GfInterval interval(time.start(), time.end(), false, false);
                xform.GetTimeSamplesInInterval(interval, &timeSamples);
                break;
            }
            if (checkParents) {
                primIt = primIt.GetParent();
            } else {
                break;
            }
        }
        // Add the boundaries to the timeSamples
        timeSamples.push_back(static_cast<double>(time.motionStart));
        timeSamples.push_back(static_cast<double>(time.motionEnd));

        // Remove duplicates as the time samples found can be at the boundaries
        std::sort(timeSamples.begin(), timeSamples.end());
        timeSamples.erase(std::unique(timeSamples.begin(), timeSamples.end()), timeSamples.end());

        // We use the number of actual keys in the interval as the number of timesamples for Arnold.
        // It's a heuristic in the sense that some of the keys might be localized around a particular time
        // and when we are going to resample with a uniform distribution, we might get less precision in that area
        // whereas we'll have too much precision in the other.
        numKeys = timeSamples.size();
    }

    // If this prim is an instancer, we want to take into account the instances transform keys
    UsdGeomPointInstancer pointInstancer(prim);
    if (pointInstancer) {
        const bool hasVelocities = pointInstancer.GetVelocitiesAttr().HasAuthoredValue() || 
                            pointInstancer.GetAngularVelocitiesAttr().HasAuthoredValue() || 
                            pointInstancer.GetAccelerationsAttr().HasAuthoredValue();
        // In case we have velocities, we sample only at the boundaries, so just 2 keys
        if (hasVelocities) {
            numKeys = 2;
        } else {
            numKeys = std::max(numKeys, ComputeNumKeys(pointInstancer.GetPositionsAttr(), time));
            numKeys = std::max(numKeys, ComputeNumKeys(pointInstancer.GetOrientationsAttr(), time));
            numKeys = std::max(numKeys, ComputeNumKeys(pointInstancer.GetScalesAttr(), time));
        }
    }
    return numKeys;
}

struct PrimvarValueReader : public ValueReader
{
public:
    PrimvarValueReader(const UsdGeomPrimvar& primvar,
        bool computeFlattened = false, PrimvarsRemapper *primvarsRemapper = nullptr, 
        TfToken primvarInterpolation = TfToken()) :
        ValueReader(),
        _primvar(primvar),
        _computeFlattened(computeFlattened),
        _primvarsRemapper(primvarsRemapper),
        _primvarInterpolation(primvarInterpolation)        
    {        
    }

    bool Get(VtValue *value, double time) override {
        if (value == nullptr)
            return false;

        bool res = false;
        if (_computeFlattened) {
            res = _primvar.ComputeFlattened(value, time);
            
        } 
        else {
            res = _primvar.Get(value, time);
        }
        
        if (_primvarsRemapper)
            _primvarsRemapper->RemapValues(_primvar, _primvarInterpolation, *value);

        return res;
    }
    
protected:
    const UsdGeomPrimvar &_primvar;
    bool _computeFlattened = false;
    PrimvarsRemapper *_primvarsRemapper = nullptr;
    TfToken _primvarInterpolation;
};


/**
 *  Read all primvars from this shape, and set them as arnold user data
 *
 **/

void ReadPrimvars(
    const UsdPrim &prim, AtNode *node, const TimeSettings &time, ArnoldAPIAdapter &context, 
    PrimvarsRemapper *primvarsRemapper)
{
    assert(prim);
    UsdGeomPrimvarsAPI primvarsAPI = UsdGeomPrimvarsAPI(prim);
    if (!primvarsAPI)
        return;

    float frame = time.frame;
    // copy the time settings, as we might want to disable motion blur
    TimeSettings attrTime(time);
    
    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);
    bool isPolymesh = AiNodeIs(node, str::hdpolymesh);
    bool isPoints = (isPolymesh) ? false : AiNodeIs(node, str::points);
    
    // First, we'll want to consider all the primvars defined in this primitive
    std::vector<UsdGeomPrimvar> primvars = primvarsAPI.GetPrimvars();
    size_t primvarsSize = primvars.size();
    // Then, we'll also want to use the primvars that were accumulated over this prim hierarchy,
    // and that only included constant primvars. Note that all the constant primvars defined in 
    // this primitive will appear twice in the full primvars list, so we'll skip them during the loop
    const std::vector<UsdGeomPrimvar> &inheritedPrimvars = context.GetPrimvars();
    primvars.insert(primvars.end(), inheritedPrimvars.begin(), inheritedPrimvars.end());

    for (size_t i = 0; i < primvars.size(); ++i) {
        const UsdGeomPrimvar &primvar = primvars[i];

        // ignore primvars starting with arnold as they will be loaded separately.
        // same for other namespaces
        if (TfStringStartsWith(primvar.GetName().GetString(), str::t_primvars_arnold))
            continue;

        TfToken interpolation = primvar.GetInterpolation();
        // Find the declaration based on the interpolation type
        std::string declaration =
            (interpolation == UsdGeomTokens->uniform)
                ? "uniform"
                : (interpolation == UsdGeomTokens->varying)
                      ? "varying"
                      : (interpolation == UsdGeomTokens->vertex)
                            ? "varying"
                            : (interpolation == UsdGeomTokens->faceVarying) ? "indexed" : "constant";


        // We want to ignore the constant primvars returned by primvarsAPI.GetPrimvars(),
        // because they'll also appear in the second part of the list, coming from inheritedPrimvars
        if (i < primvarsSize && interpolation == UsdGeomTokens->constant)
            continue;
        
        TfToken name = primvar.GetPrimvarName();
        if ((name == "displayColor" || name == "displayOpacity" || name == "normals") && !primvar.GetAttr().HasAuthoredValue())
            continue;

        // if this parameter already exists, we want to skip it
        if (AiNodeEntryLookUpParameter(nodeEntry, AtString(name.GetText())) != nullptr)
            continue;

        // A remapper can eventually remap the interpolation (e.g. point instancer)
        if (primvarsRemapper) {
            if (!primvarsRemapper->ReadPrimvar(name))
                continue;
            primvarsRemapper->RemapPrimvar(name, declaration);
        }

        SdfValueTypeName typeName = primvar.GetTypeName();        
        std::string arnoldIndexName = name.GetText() + std::string("idxs");

        int primvarType = AI_TYPE_NONE;

        //  In Arnold, points with user-data per-point are considered as being "uniform" (one value per face).
        //  We must ensure that we're not setting varying user data on the points or this will fail (see #228)
        if (isPoints && declaration == "varying")
            declaration = "uniform";

        if (typeName == SdfValueTypeNames->Float2 || typeName == SdfValueTypeNames->Float2Array ||
            typeName == SdfValueTypeNames->TexCoord2f || typeName == SdfValueTypeNames->TexCoord2fArray) {
            primvarType = AI_TYPE_VECTOR2;

            // A special case for UVs
            if (isPolymesh && (name == "uv" || name == "st")) {
                name = str::t_uvlist;
                // Arnold doesn't support motion blurred UVs, this is causing an error (#780),
                // let's disable it for this attribute
                attrTime.motionBlur = false;
                arnoldIndexName = "uvidxs";
                // In USD the uv coordinates can be per-vertex. In that case we won't have any "uvidxs"
                // array to give to the arnold polymesh, and arnold will error out. We need to set an array
                // that is identical to "vidxs" and returns the vertex index for each face-vertex
                if (interpolation == UsdGeomTokens->varying || (interpolation == UsdGeomTokens->vertex)) {
                    AiNodeSetArray(node, str::uvidxs, AiArrayCopy(AiNodeGetArray(node, str::vidxs)));
                }
            }
        } else if (
            typeName == SdfValueTypeNames->Vector3f || typeName == SdfValueTypeNames->Vector3fArray ||
            typeName == SdfValueTypeNames->Point3f || typeName == SdfValueTypeNames->Point3fArray ||
            typeName == SdfValueTypeNames->Normal3f || typeName == SdfValueTypeNames->Normal3fArray ||
            typeName == SdfValueTypeNames->Float3 || typeName == SdfValueTypeNames->Float3Array ||
            typeName == SdfValueTypeNames->TexCoord3f || typeName == SdfValueTypeNames->TexCoord3fArray) {
            primvarType = AI_TYPE_VECTOR;
        } else if (typeName == SdfValueTypeNames->Color3f || typeName == SdfValueTypeNames->Color3fArray)
            primvarType = AI_TYPE_RGB;
        else if (
            typeName == SdfValueTypeNames->Color4f || typeName == SdfValueTypeNames->Color4fArray ||
            typeName == SdfValueTypeNames->Float4 || typeName == SdfValueTypeNames->Float4Array)
            primvarType = AI_TYPE_RGBA;
        else if (typeName == SdfValueTypeNames->Float || typeName == SdfValueTypeNames->FloatArray || 
            typeName == SdfValueTypeNames->Double || typeName == SdfValueTypeNames->DoubleArray)
            primvarType = AI_TYPE_FLOAT;
        else if (typeName == SdfValueTypeNames->Int || typeName == SdfValueTypeNames->IntArray)
            primvarType = AI_TYPE_INT;
        else if (typeName == SdfValueTypeNames->UInt || typeName == SdfValueTypeNames->UIntArray)
            primvarType = AI_TYPE_UINT;
        else if (typeName == SdfValueTypeNames->UChar || typeName == SdfValueTypeNames->UCharArray)
            primvarType = AI_TYPE_BYTE;
        else if (typeName == SdfValueTypeNames->Bool || typeName == SdfValueTypeNames->BoolArray)
            primvarType = AI_TYPE_BOOLEAN;
        else if (typeName == SdfValueTypeNames->String || typeName == SdfValueTypeNames->StringArray) {
            // both string and node user data are saved to USD as string attributes, since there's no
            // equivalent in USD. To distinguish between these 2 use cases, we will also write a
            // connection between the string primvar and the node. This is what we use here to
            // determine the user data type.
            primvarType = (primvar.GetAttr().HasAuthoredConnections()) ? AI_TYPE_NODE : AI_TYPE_STRING;
        }

        if (primvarType == AI_TYPE_NONE)
            continue;

        int arrayType = AI_TYPE_NONE;
        
        if (typeName.IsArray() && interpolation == UsdGeomTokens->constant &&
            primvarType != AI_TYPE_ARRAY && primvar.GetElementSize() > 1) 
        {
            arrayType = primvarType;
            primvarType = AI_TYPE_ARRAY;
            declaration += " ARRAY ";
        }

        declaration += " ";
        declaration += AiParamGetTypeName(primvarType);

        
        AtString nameStr(name.GetText());
        if (AiNodeLookUpUserParameter(node, nameStr) == nullptr && 
            AiNodeEntryLookUpParameter(nodeEntry, nameStr) == nullptr) {
            AiNodeDeclare(node, nameStr, declaration.c_str());    
        }
            
        bool hasIdxs = false;

        // If the primvar is indexed, we need to set this as a
        if (interpolation == UsdGeomTokens->faceVarying) {
            VtIntArray vtIndices;
            std::vector<unsigned int> indexes;

            if (primvar.IsIndexed() && primvar.GetIndices(&vtIndices, frame) && !vtIndices.empty()) {
                // We need to use indexes and we can't use vtIndices because we
                // need unsigned int. Converting int to unsigned int.
                indexes.resize(vtIndices.size());
                std::copy(vtIndices.begin(), vtIndices.end(), indexes.begin());
            } else {
                // Arnold doesn't have facevarying iterpolation. It has indexed
                // instead. So it means it's necessary to generate indexes for
                // this type.
                // TODO: Try to generate indexes only once and use it for
                // several primvars.

                // Unfortunately elementSize is not giving us the value we need here,
                // so we need to get the VtValue just to find its size.
                VtValue tmp;                    
                if (primvar.Get(&tmp, time.frame)) {
                    indexes.resize(tmp.GetArraySize());
                    // Fill it with 0, 1, ..., 99.
                    std::iota(std::begin(indexes), std::end(indexes), 0);
                }
            }
            if (!indexes.empty())
            {
                // If the mesh has left-handed orientation, we need to invert the
                // indices of primvars for each face
                if (primvarsRemapper)
                    primvarsRemapper->RemapIndexes(primvar, interpolation, indexes);
                
                AiNodeSetArray(
                    node, AtString(arnoldIndexName.c_str()), AiArrayConvert(indexes.size(), 1, AI_TYPE_UINT, indexes.data()));

                hasIdxs = true;
            }
        }

        // Deduce primvar type and array type.
        if (interpolation != UsdGeomTokens->constant && primvarType != AI_TYPE_ARRAY) {
            arrayType = primvarType;
            primvarType = AI_TYPE_ARRAY;
        
        }

        bool computeFlattened = (interpolation != UsdGeomTokens->constant && !hasIdxs);
        PrimvarValueReader valueReader(primvar, computeFlattened, primvarsRemapper, interpolation);
        InputAttribute inputAttr;
        CreateInputAttribute(inputAttr, primvar.GetAttr(), attrTime, primvarType, arrayType, &valueReader);
        ReadAttribute(inputAttr, node, name.GetText(), attrTime, context, primvarType, arrayType);
    }
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
