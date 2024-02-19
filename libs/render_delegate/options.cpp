//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2019 Luma Pictures
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required ~by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Modifications Copyright 2022 Autodesk, Inc.
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
#include "options.h"

#include <pxr/usdImaging/usdImaging/tokens.h>

#include <constant_strings.h>
#include "debug_codes.h"
#include "hdarnold.h"
#include "camera.h"
#include "utils.h"

#include <ai.h>
#include <iostream>
#include <unordered_map>


PXR_NAMESPACE_OPEN_SCOPE

HdArnoldOptions::HdArnoldOptions(HdArnoldRenderDelegate* renderDelegate, const SdfPath& id)
    : HdSprim(id), _renderDelegate(renderDelegate)
{
}

HdArnoldOptions::~HdArnoldOptions()
{   
    _renderDelegate->ClearDependencies(GetId());
}

// Root function called to translate a shading NodeGraph primitive
void HdArnoldOptions::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{    
    const auto id = GetId();
    if ((*dirtyBits & HdArnoldOptions::DirtyParams) && !id.IsEmpty()) {
        HdArnoldRenderParamInterrupt param(renderParam);

        const AtNodeEntry *optionsNodeEntry = AiNodeEntryLookUp(str::options);
        if (optionsNodeEntry == nullptr)
            return;

        AtNode* options = AiUniverseGetOptions(_renderDelegate->GetUniverse());
        const AtNodeEntry* nodeEntry = AiNodeGetNodeEntry(options);

        AtParamIterator* paramIter = AiNodeEntryGetParamIterator(nodeEntry);
        while (!AiParamIteratorFinished(paramIter)) {
            const AtParamEntry* param = AiParamIteratorGetNext(paramIter);
            const AtString paramName = AiParamGetName(param);
            if (paramName == str::outputs)
                continue;
            const TfToken paramToken = TfToken{TfStringPrintf("arnold:%s", paramName.c_str())};

            const VtValue val = sceneDelegate->Get(id, paramToken);
            if (!val.IsEmpty()) {
                if (paramName == str::camera) {
                    // For cameras, we need to look for the right render camera
                    // and set the connection in the options node
                    std::string camera = VtValueGetString(val);
                    HdArnoldCamera* cameraNode = reinterpret_cast<HdArnoldCamera*>(
                        sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->camera, SdfPath(camera.c_str())));
                                        
                    if (cameraNode) {
                        cameraNode->Sync(sceneDelegate, renderParam, dirtyBits);
                        AtNode* camNode = cameraNode->GetCamera();
                        AiNodeSetPtr(options, str::camera, (void*)camNode);
                    }
                } else {
                    HdArnoldSetParameter(options, param, val, _renderDelegate);
                }
            }            
        }
        AiParamIteratorDestroy(paramIter);
    }
    *dirtyBits = HdArnoldOptions::Clean;
}

HdDirtyBits HdArnoldOptions::GetInitialDirtyBitsMask() const { return HdArnoldOptions::DirtyParams; }


PXR_NAMESPACE_CLOSE_SCOPE
