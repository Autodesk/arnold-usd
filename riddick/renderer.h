#pragma once

#include <string>
#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"

/*
 * This is the main function to render from hydra.
 */

PXR_NAMESPACE_OPEN_SCOPE

void RenderToFile(
    UsdStageRefPtr stage, int width, int height, const SdfPath &cameraId, const std::string &outputImagePath);

PXR_NAMESPACE_CLOSE_SCOPE
