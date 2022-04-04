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
#include "write_arnold_type.h"

#include <ai.h>

#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdGeom/boundable.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((frame, "arnold:frame"))
);

/**
 *    Write out any Arnold node to a generic "typed"  USD primitive (eg
 *ArnoldSetParameter, ArnoldDriverExr, etc...). In this write function, we need
 *to create the USD primitive, then loop over the Arnold node attributes, and
 *write them to the USD file. Note that we could (should) use the schemas to do
 *this, but since the conversion is simple, for now we're hardcoding it here.
 *For now the attributes are prefixed with "arnold:" as this is what is done in
 *the schemas. But this is something that we could remove in the future, as it's
 *not strictly needed.
 **/
void UsdArnoldWriteArnoldType::Write(const AtNode *node, UsdArnoldWriter &writer)
{
     // get the output name of this USD primitive 
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // get the current stage defined in the writer
    SdfPath objPath(nodeName);

    const AtNodeEntry *nodeEntry = AiNodeGetNodeEntry(node);

    int nodeEntryType = AiNodeEntryGetType(nodeEntry);
    bool isXformable = (nodeEntryType == AI_NODE_SHAPE 
        || nodeEntryType == AI_NODE_CAMERA || nodeEntryType == AI_NODE_LIGHT);
    
    if (isXformable)
        writer.CreateHierarchy(objPath);
    
    UsdPrim prim = stage->DefinePrim(objPath, TfToken(_usdName));

    // For arnold nodes that have a transform matrix, we read it as in a 
    // UsdGeomXformable
    if (isXformable)
    {
        UsdGeomXformable xformable(prim);
        _WriteMatrix(xformable, node, writer);
        // If this arnold node is a shape, let's write the material bindings
        if (nodeEntryType == AI_NODE_SHAPE) {
            _WriteMaterialBinding(node, prim, writer);

             if (AiNodeEntryGetDerivedType(nodeEntry) == AI_NODE_SHAPE_PROCEDURAL) {
                // For procedurals, we also want to write out the extents attribute
                AtUniverse *universe = AiUniverse();
                AtParamValueMap *params = AiParamValueMap();
                AiParamValueMapSetInt(params, AtString("mask"), AI_NODE_SHAPE);
                AiProceduralViewport(node, universe, AI_PROC_BOXES, params);
                AiParamValueMapDestroy(params);
                AtBBox bbox;
                bbox.init();
                static AtString boxStr("box");

                // Need to loop over all the nodes that were created in this "viewport" 
                // universe, and get an expanded bounding box
                AtNodeIterator* nodeIter = AiUniverseGetNodeIterator(universe, AI_NODE_SHAPE);
                while (!AiNodeIteratorFinished(nodeIter))
                {
                    AtNode *node = AiNodeIteratorGetNext(nodeIter);
                    if (AiNodeIs(node, boxStr)) {
                        bbox.expand(AiNodeGetVec(node, AtString("min")));
                        bbox.expand(AiNodeGetVec(node, AtString("max")));
                    }
                }
                AiNodeIteratorDestroy(nodeIter);
                AiUniverseDestroy(universe);

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
        }
    }
    _WriteArnoldParameters(node, writer, prim, "arnold");
}

void UsdArnoldWriteGinstance::_ProcessInstanceAttribute(
    UsdPrim &prim, const AtNode *node, const AtNode *target, 
    const char *attrName, int attrType, UsdArnoldWriter &writer)
{
    if (AiNodeEntryLookUpParameter(AiNodeGetNodeEntry(target), AtString(attrName)) == nullptr)
        return; // the attribute doesn't exist in the instanced node

    // Now compare the values between the ginstance and the target node. If the value
    // is different we'll want to write it even though it's the default value
    bool writeValue = false;
    SdfValueTypeName usdType;
    if (attrType == AI_TYPE_BOOLEAN) {
        writeValue = (AiNodeGetBool(node, AtString(attrName)) != AiNodeGetBool(target, AtString(attrName)));
        usdType = SdfValueTypeNames->Bool;
    } else if (attrType == AI_TYPE_BYTE) {
        writeValue = (AiNodeGetByte(node, AtString(attrName)) != AiNodeGetByte(target, AtString(attrName)));
        usdType = SdfValueTypeNames->UChar;
    } else
        return;

    if (writeValue) {
        UsdAttribute attr = prim.CreateAttribute(TfToken(attrName), usdType, false);
        if (attrType == AI_TYPE_BOOLEAN)
            writer.SetAttribute(attr, AiNodeGetBool(node, AtString(attrName)));
        else if (attrType == AI_TYPE_BYTE)
            writer.SetAttribute(attr, AiNodeGetByte(node, AtString(attrName)));
    }
    _exportedAttrs.insert(attrName);
}

void UsdArnoldWriteGinstance::Write(const AtNode *node, UsdArnoldWriter &writer)
{
    // get the output name of this USD primitive
    std::string nodeName = GetArnoldNodeName(node, writer);
    UsdStageRefPtr stage = writer.GetUsdStage();    // get the current stage defined in the writer
    SdfPath objPath(nodeName);

    writer.CreateHierarchy(objPath);
    UsdPrim prim = stage->DefinePrim(objPath, TfToken(_usdName));

    AtNode *target = (AtNode *)AiNodeGetPtr(node, AtString("node"));
    if (target) {
        _ProcessInstanceAttribute(prim, node, target, "visibility", AI_TYPE_BYTE, writer);
        _ProcessInstanceAttribute(prim, node, target, "sidedness", AI_TYPE_BYTE, writer);
        _ProcessInstanceAttribute(prim, node, target, "matte", AI_TYPE_BOOLEAN, writer);
        _ProcessInstanceAttribute(prim, node, target, "receive_shadows", AI_TYPE_BOOLEAN, writer);
        _ProcessInstanceAttribute(prim, node, target, "invert_normals", AI_TYPE_BOOLEAN, writer);
        _ProcessInstanceAttribute(prim, node, target, "self_shadows", AI_TYPE_BOOLEAN, writer);

        writer.WritePrimitive(target);
        std::string targetName = GetArnoldNodeName(target, writer);
        SdfPath targetPath(targetName);
        UsdPrim targetPrim = stage->GetPrimAtPath(targetPath);
        UsdGeomBoundable targetBoundable(targetPrim);
        UsdAttribute extentsAttr = targetBoundable.GetExtentAttr();
        if (extentsAttr) {
            VtVec3fArray extents;
            extentsAttr.Get(&extents, (float)writer.GetTime().GetValue());

            UsdGeomBoundable boundable(prim);
            writer.SetAttribute(boundable.CreateExtentAttr(), extents);
        }
    }
    UsdGeomXformable xformable(prim);
    _WriteMatrix(xformable, node, writer);
    _WriteMaterialBinding(node, prim, writer);

    _WriteArnoldParameters(node, writer, prim, "arnold");
}
