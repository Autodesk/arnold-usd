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
    void CreateAdapters(UsdArnoldReaderContext &context, const std::string &primName);
    const std::vector<UsdTimeCode>& GetTimes() const;
    bool IsValid() const;
private:
    UsdArnoldSkelDataImpl *_impl;
};
