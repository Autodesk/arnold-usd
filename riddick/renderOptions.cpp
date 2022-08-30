#include "renderOptions.h"
#include <iostream>
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdGeom/camera.h"

/*
 */
PXR_NAMESPACE_OPEN_SCOPE

// Helper to skip command line argument
#define NEXT_ARG                                  \
    arg++;                                        \
    if (arg >= argc) {                            \
        std::cerr << "bad argument" << std::endl; \
        break;                                    \
    }

void RenderOptions::UpdateFromCommandLine(int argc, char **argv)
{
    // We mostly support the arguments used in the test suite.
    // See: tools/utils/regression_tests.py
    for (int arg = 1; arg < argc; ++arg) {
        // Parse the command line
        if (strcmp(argv[arg], "-r") == 0) {
            NEXT_ARG
            imageWidth = atoi(argv[arg]);
            NEXT_ARG
            imageHeight = atoi(argv[arg]);
        } else if (strcmp(argv[arg], "-c") == 0) {
            NEXT_ARG
            cameraPath = argv[arg];
        } else if (strcmp(argv[arg], "-o") == 0) {
            NEXT_ARG
            outputImageFileName = argv[arg];
        } else if (strcmp(argv[arg], "-frame") == 0) {
            NEXT_ARG
            frameTimeCode = atof(argv[arg]);
        } else if (strcmp(argv[arg], "-dp") == 0) {
            disableProgressingRendering = true;
        } else if (strcmp(argv[arg], "-dw") == 0) {
            // Disable render and error report windows
            //  SKIP
        } else if (strcmp(argv[arg], "-sm") == 0) {
            // Set the value of ai_default_reflection_shader.shade_mode
            NEXT_ARG
        } else if (strcmp(argv[arg], "-bs") == 0) {
            // Bucket size - skip for the moment
            NEXT_ARG
        } else if (strcmp(argv[arg], "-set") == 0) {
            // Set the value of a node parameter (-set name.parameter value)
            NEXT_ARG
            NEXT_ARG
        } else if (argv[arg] && (argv[arg][0] == '-')) {
            std::cerr << "unknown argument " << argv[arg] << std::endl;
        } else {
            std::cerr << "Reading filename" << argv[arg] << std::endl;
            inputSceneFileName = argv[arg];
        }
    }
}

// Update options reading the scene (or not)
void RenderOptions::UpdateFromStage(UsdStageRefPtr stage)
{
    // TODO: look for metadata giving the renderSettings to pick-up

    // First get the camera location if the camera is not set
    if (cameraPath.empty()) {
        for (const auto &prim : stage->Traverse()) {
            // Pick the first camera found
            if (prim.IsA<UsdGeomCamera>() && cameraPath.empty()) {
                cameraPath = prim.GetPath().GetString();
            }
        }
    }
}

bool RenderOptions::IsValidForOpeningStage() const
{
    if (inputSceneFileName == "") {
        std::cerr << "invalid input scene file name" << std::endl;
        return false;
    }
    return true;
}

bool RenderOptions::IsValidForRendering() const
{
    if (outputImageFileName == "") {
        std::cerr << "invalid output image file name" << std::endl;
        return false;
    }
    if (cameraPath == "") {
        std::cerr << "invalid camera path" << std::endl;
        return false;
    }
    if (imageWidth<=0 || imageHeight <= 0) {
        std::cerr << "invalid image size " << imageWidth << "x" << imageHeight << std::endl;
        return false;
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
