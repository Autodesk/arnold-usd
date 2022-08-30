#pragma once

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

    //
    int imageWidth = 160; // -1 means that the value is not initialized
    int imageHeight = 120;
    float frameTimeCode = 1.f;
    bool disableProgressingRendering = false;
    std::string inputSceneFileName;
    std::string outputImageFileName;
    std::string cameraPath;
};

PXR_NAMESPACE_CLOSE_SCOPE
