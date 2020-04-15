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
#include <string.h>
#include <iostream>

#include "writer.h"

#include <ai.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/pxr.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/xform.h"

/**
 *  Small utility command that converts an Arnold input .ass file into a .usd
 *file. It uses the "writer" translator to do it.
 **/
int main(int argc, char** argv)
{
    if (argc < 3)
        return -1;

    std::string assname = argv[1]; // 1st command-line argument is the input .ass file
    std::string usdname = argv[2]; // 2nd command-line argument is the output .usd file

    // Start the Arnold session, and load the input .ass file
    AiBegin(AI_SESSION_INTERACTIVE);
    AiASSLoad(assname.c_str());

    // Create a new USD stage to write out the .usd file
    UsdStageRefPtr stage = UsdStage::Open(SdfLayer::CreateNew(usdname));

    // Create a "writer" Translator that will handle the conversion
    UsdArnoldWriter* writer = new UsdArnoldWriter();
    writer->SetUsdStage(stage);    // give it the output stage
    writer->Write(nullptr);        // do the conversion (nullptr being the default universe)
    stage->GetRootLayer()->Save(); // Ask USD to save out the file
    AiEnd();
    return 0;
}
