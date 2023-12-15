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
#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <pxr/usd/usdShade/nodeGraph.h>
#include <pxr/usd/usd/primCompositionQuery.h>

#include <pxr/usd/pcp/layerStack.h>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

#include <constant_strings.h>

#include "prim_reader.h"

//-*************************************************************************

PXR_NAMESPACE_USING_DIRECTIVE

// Unsupported node types should dump a warning when being converted
AtNode* UsdArnoldReadUnsupported::Read(const UsdPrim &prim, UsdArnoldReaderContext &context)
{
    AiMsgWarning(
        "UsdArnoldReader : %s primitives not supported, cannot read %s", _typeName.c_str(), prim.GetName().GetText());

    return nullptr;
}
