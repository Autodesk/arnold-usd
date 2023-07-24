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
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/scriptModuleLoader.h>
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfScriptModuleLoader)
{
    const std::vector<TfToken> reqs = {
        TfToken("ar"),      TfToken("arch"),     TfToken("gf"),     TfToken("js"), TfToken("kind"),
        TfToken("pcp"),     TfToken("plug"),     TfToken("sdf"),    TfToken("tf"), TfToken("usd"),
        TfToken("usdGeom"), TfToken("usdShade"), TfToken("usdLux"), TfToken("vt"), TfToken("work"),
    };
    TfScriptModuleLoader::GetInstance().RegisterLibrary(TfToken("usdArnold"), TfToken("UsdArnold"), reqs);
}

PXR_NAMESPACE_CLOSE_SCOPE
