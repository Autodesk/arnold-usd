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

#pragma once



#include "pxr/pxr.h"
#include "pxr/usd/usdSkel/api.h"

#include "pxr/base/gf/interval.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/types.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdSkel/root.h"
#include "pxr/usd/usdSkel/cache.h"

#include "pxr/usd/usdSkel/binding.h"


#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class UsdArnoldReaderContext;

/// Parameters for configuring ArnoldUsdSkelBakeSkinning.
struct ArnoldUsdSkelBakeSkinningParms
{
    /// Flags for identifying different deformation paths.
    enum DeformationFlags {
        DeformPointsWithLBS = 1 << 0,
        DeformNormalsWithLBS = 1 << 1,
        DeformXformWithLBS = 1 << 2,
        DeformPointsWithBlendShapes = 1 << 3,
        DeformNormalsWithBlendShapes = 1 << 4,
        DeformWithLBS = (DeformPointsWithLBS|
                         DeformNormalsWithLBS|
                         DeformXformWithLBS),
        DeformWithBlendShapes = (DeformPointsWithBlendShapes|
                                 DeformNormalsWithBlendShapes),
        DeformAll = DeformWithLBS|DeformWithBlendShapes,
        /// Flags indicating which components of skinned prims may be
        /// modified, based on the active deformations.
        ModifiesPoints = DeformPointsWithLBS|DeformPointsWithBlendShapes,
        ModifiesNormals = DeformNormalsWithLBS|DeformNormalsWithBlendShapes,
        ModifiesXform = DeformXformWithLBS
    };

    /// Flags determining which deformation paths are enabled.
    int deformationFlags = DeformAll;
    
    /// Memory limit for pending stage writes, given in bytes.
    /// If zero, memory limits are ignored. Otherwise, output stages
    /// are flushed each time pending writes exceed this amount.
    /// Note that at least one frame of data for *all* skinned prims
    /// will be held in memory prior to values being written to disk,
    /// regardless of this memory limit.    
    /// Since flushing pending changes requires layers to be saved,
    /// memory limiting is only active when _saveLayers_ is enabled.
    size_t memoryLimit = 0;

    /// If true, extents of UsdGeomPointBased-derived prims are updated
    /// as new skinned values are produced. This is made optional
    /// in case an alternate procedure is being used to compute
    /// extents elsewhere.
    bool updateExtents = true;

    /// If true, extents hints of models that already stored
    /// an extentsHint are updated to reflect skinning changes.
    /// All extent hints are authored to the stage's current edit target.
    bool updateExtentHints = true;

    /// The set of bindings to bake.
    std::vector<UsdSkelBinding> bindings;
    
};


struct UsdArnoldSkelDataImpl;

class UsdArnoldSkelData {
public:
    UsdArnoldSkelData(const UsdPrim &prim);
    UsdArnoldSkelData(const UsdArnoldSkelData &src);
    ~UsdArnoldSkelData();

    enum SkinningData {
        SKIN_POINTS = 0,
        SKIN_NORMALS
    };

    bool ApplyPointsSkinning(const UsdPrim &prim, const VtArray<GfVec3f> &input, VtArray<GfVec3f> &output, UsdArnoldReaderContext &context, double time, SkinningData s);
    void CreateAdapters(UsdArnoldReaderContext &context, const UsdPrim *prim = nullptr);
    const std::vector<UsdTimeCode>& GetTimes() const;
    bool IsValid() const;
private:
    UsdArnoldSkelDataImpl *_impl;
};
