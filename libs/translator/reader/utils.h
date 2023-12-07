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
#pragma once

#include <ai_nodes.h>

#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdShade/shader.h>

#include <numeric>
#include <string>
#include <vector>

#include <parameters_utils.h>
#include "../utils/utils.h"
#include <shape_utils.h>

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldReader;
class UsdArnoldReaderContext;

#include "timesettings.h"

/** Read Xformable transform as an arnold shape "matrix"
 */
void ReadMatrix(const UsdPrim& prim, AtNode* node, const TimeSettings& time, 
    UsdArnoldReaderContext& context, bool isXformable=true);

AtArray *ReadMatrix(const UsdPrim& prim, const TimeSettings& time, 
    UsdArnoldReaderContext& context, bool isXformable=true);

AtArray *ReadLocalMatrix(const UsdPrim &prim, const TimeSettings &time);



size_t ReadTopology(
    UsdAttribute& usdAttr, AtNode* node, const char* attrName, const TimeSettings& time, UsdArnoldReaderContext &context);
/**
 *  Read all primvars from this shape, and set them as arnold user data
 *
 **/
void ReadPrimvars(
        const UsdPrim &prim, AtNode *node, const TimeSettings &time, ArnoldAPIAdapter &context,
        PrimvarsRemapper *primvarsRemapper = nullptr);

// Read the materials / shaders assigned to a shape (node)
void ReadMaterialBinding(const UsdPrim& prim, AtNode* node, UsdArnoldReaderContext& context, bool assignDefault = true);

// Read the materials / shaders assigned to a shape (node)
void ReadSubsetsMaterialBinding(
    const UsdPrim& prim, AtNode* node, UsdArnoldReaderContext& context, std::vector<UsdGeomSubset>& subsets,
    unsigned int elementCount, bool assignDefault = true);




bool IsPrimVisible(const UsdPrim &prim, UsdArnoldReader *reader, float frame);

void ApplyParentMatrices(AtArray *matrices, const AtArray *parentMatrices);

void ReadLightShaders(const UsdPrim& prim, const UsdAttribute &attr, AtNode *node, UsdArnoldReaderContext &context);
void ReadCameraShaders(const UsdPrim& prim, AtNode *node, UsdArnoldReaderContext &context);

// The normals can be set on primvars:normals or just normals. 
// primvars:normals takes "precedence" over "normals"
template <typename UsdGeomT>
inline UsdAttribute GetNormalsAttribute(const UsdGeomT &usdGeom) {
    UsdGeomPrimvarsAPI primvarsAPI(usdGeom.GetPrim());
    if (primvarsAPI) {
        UsdGeomPrimvar normalsPrimvar = primvarsAPI.GetPrimvar(TfToken("normals"));
        if (normalsPrimvar) {
            return normalsPrimvar.GetAttr();
        }
    }
    return usdGeom.GetNormalsAttr();
}

template <typename UsdGeomT>
inline TfToken GetNormalsInterpolation(const UsdGeomT &usdGeom) {
    UsdGeomPrimvarsAPI primvarsAPI(usdGeom.GetPrim());
    if (primvarsAPI) {
        UsdGeomPrimvar normalsPrimvar = primvarsAPI.GetPrimvar(TfToken("normals"));
        if (normalsPrimvar) {
            return normalsPrimvar.GetInterpolation();
        }
    }
    return usdGeom.GetNormalsInterpolation();
}

int GetTimeSampleNumKeys(const UsdPrim &geom, const TimeSettings &tim, TfToken interpolation=UsdGeomTokens->constant);
