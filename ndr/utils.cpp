// Copyright 2019 Luma Pictures
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
//
// Modifications Copyright 2019 Autodesk, Inc.
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

#include <pxr/base/tf/getenv.h>

#include <pxr/usd/usd/prim.h>

#include <ai.h>

PXR_NAMESPACE_OPEN_SCOPE

UsdStageRefPtr NdrArnoldGetShaderDefs() {
    static auto ret = [] () -> UsdStageRefPtr {
        auto stage = UsdStage::CreateInMemory("__ndrArnoldShaderDefs.usda");

        const auto hasActiveUniverse = AiUniverseIsActive();
        if (!hasActiveUniverse) {
            AiBegin(AI_SESSION_BATCH);
            AiMsgSetConsoleFlags(AI_LOG_NONE);
            auto arnoldPluginPath = TfGetenv("ARNOLD_PLUGIN_PATH", "");
            if (!arnoldPluginPath.empty()) {
                AiLoadPlugins(arnoldPluginPath.c_str());
            }
        }

        auto* nodeIter = AiUniverseGetNodeEntryIterator(AI_NODE_SHADER);

        while (AiNodeEntryIteratorFinished(nodeIter)) {
            auto* nodeEntry = AiNodeEntryIteratorGetNext(nodeIter);
            auto prim = stage->DefinePrim(SdfPath(TfStringPrintf("/%s", AiNodeEntryGetName(nodeEntry))));
        }

        AiNodeEntryIteratorDestroy(nodeIter);

        if (!hasActiveUniverse) {
            AiEnd();
        }

        return stage;
    } ();
    return ret;
}

PXR_NAMESPACE_CLOSE_SCOPE
