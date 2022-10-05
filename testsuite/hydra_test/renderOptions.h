#pragma once
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

#include <string>
#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"

/**
 * RenderOptions
 *   - stores the parameters used for rendering.
 *   - parses the command line for render parameters.
 *   - scan the stage for render parameters, like camera or RenderSettings prims
 */

PXR_NAMESPACE_OPEN_SCOPE

struct RenderOptions {
    // Read the command line arguments and update this structure
    void UpdateFromCommandLine(int argc, char **argv);

    // Read the stage render settings and update this structure
    void UpdateFromStage(UsdStageRefPtr stage);

    //
    // Validity checks
    //
    bool IsValidForOpeningStage() const;
    bool IsValidForRendering() const;

    // The actual parameters
    int imageWidth = 160;
    int imageHeight = 120;
    float frameTimeCode = 1.f;
    bool disableProgressingRendering = false;
    std::string inputSceneFileName;
    std::string outputImageFileName;
    std::string cameraPath;
};

PXR_NAMESPACE_CLOSE_SCOPE
