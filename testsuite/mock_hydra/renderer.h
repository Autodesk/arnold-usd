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

/*
 * This is the main function to render from hydra.
 */

PXR_NAMESPACE_OPEN_SCOPE

void RenderToFile(
    UsdStageRefPtr stage, int width, int height, const UsdTimeCode &timeCode, const SdfPath &cameraId, const std::string &outputImagePath);

PXR_NAMESPACE_CLOSE_SCOPE
