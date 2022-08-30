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
#include <string.h>
#include <iostream>

#include <ai.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/pxr.h>
#include <string>
#include <vector>

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/camera.h"
#include "renderOptions.h"

#include "renderer.h"

/**
  riddick : RenderDelegate kick
 **/

PXR_NAMESPACE_USING_DIRECTIVE

int main(int argc, char** argv)
{
    // Parse command line for render options
    RenderOptions options;
    options.UpdateFromCommandLine(argc, argv);

    // Check we have enought information to open a stage 
    if (!options.IsValidForOpeningStage()) {
        return 1;
    }
    UsdStageRefPtr stage = UsdStage::Open(options.inputSceneFileName);
    if (!stage) {
        std::cerr << "unable to load " << options.inputSceneFileName << std::endl;
        return 1;
    }

    // We want to read the render settings and other things like camera
    options.UpdateFromStage(stage);

    // Check the options are good for rendering (camera, output image, etc)
    if (!options.IsValidForRendering()) {
        return 1;
    }

    // Get timecode from render option
    UsdTimeCode timeCode(options.frameTimeCode);
    RenderToFile(
        stage, options.imageWidth, options.imageHeight, timeCode, SdfPath(TfToken(options.cameraPath)),
        options.outputImageFileName);

    return 0;
}
