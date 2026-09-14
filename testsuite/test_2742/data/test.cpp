#include <ai.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

bool CheckCompression(const char *driverName, const std::vector<std::string> &expected)
{
    AtNode *driver = AiNodeLookUpByName(nullptr, AtString(driverName));
    if (driver == nullptr) {
        std::cerr << "Driver " << driverName << " not found" << std::endl;
        return false;
    }
    AtArray *array = AiNodeGetArray(driver, AtString("compression"));
    const unsigned int numElements = array ? AiArrayGetNumElements(array) : 0;
    bool success = (numElements == expected.size());
    for (unsigned int i = 0; success && i < numElements; ++i)
        success = (std::string(AiArrayGetStr(array, i).c_str()) == expected[i]);

    if (!success) {
        std::cerr << driverName << " compression is [";
        for (unsigned int i = 0; i < numElements; ++i)
            std::cerr << (i ? " " : "") << AiArrayGetStr(array, i).c_str();
        std::cerr << "] instead of [";
        for (size_t i = 0; i < expected.size(); ++i)
            std::cerr << (i ? " " : "") << expected[i];
        std::cerr << "]" << std::endl;
    }
    return success;
}

bool CheckOutput(unsigned int index, const char *expected)
{
    AtArray *outputs = AiNodeGetArray(AiUniverseGetOptions(nullptr), AtString("outputs"));
    if (outputs == nullptr || AiArrayGetNumElements(outputs) <= index) {
        std::cerr << "Missing output " << index << std::endl;
        return false;
    }
    const AtString output = AiArrayGetStr(outputs, index);
    if (output != AtString(expected)) {
        std::cerr << "Output " << index << " is \"" << output.c_str()
                  << "\" instead of \"" << expected << "\"" << std::endl;
        return false;
    }
    return true;
}

bool CheckScene()
{
    bool success = true;
    // The compression array is positional, so element i lines up with the i-th output
    // referencing that driver
    success &= CheckOutput(0, "RGBA RGBA /Render/Vars/rgba/filter /Render/Products/perVar");
    success &= CheckOutput(1, "N RGB /Render/Vars/normal/filter /Render/Products/perVar");
    success &= CheckOutput(2, "Z FLOAT /Render/Vars/depth/filter /Render/Products/perVar");

    // Each RenderVar gets its own compression, the last one falls back to the driver default
    success &= CheckCompression("/Render/Products/perVar", {"dwaa", "piz", "zip"});
    // A compression authored on the RenderProduct must be left untouched
    success &= CheckCompression("/Render/Products/legacy", {"zips"});
    // RenderVars that don't author one fall back to the RenderProduct-level compression
    success &= CheckCompression("/Render/Products/mixed", {"dwab", "zips"});
    // Same, but with the driver deduced from the filename and the product-level compression
    // authored as arnold:compression
    success &= CheckCompression("/Render/Products/deduced", {"piz", "rle"});
    return success;
}

// An AtParamValueMap doesn't outlive the AiBegin/AiEnd it was created in
AtParamValueMap *CreateParams()
{
    AtParamValueMap *params = AiParamValueMap();
    AiParamValueMapSetBool(params, AtString("convert_string_outputs"), false);
    return params;
}

} // namespace

int main(int argc, char **argv)
{
    AiBegin();
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AtParamValueMap *params = CreateParams();
    AiSceneLoad(nullptr, "test.usda", params);

    bool success = CheckScene();

    AiSceneWrite(nullptr, "scene.ass", params);
    AiParamValueMapDestroy(params);
    AiEnd();

    AiBegin();
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    params = CreateParams();
    AiSceneLoad(nullptr, "scene.ass", params);
    AiParamValueMapDestroy(params);
    // The compression array must survive the .ass round trip unchanged
    success &= CheckScene();
    AiEnd();

    return success ? 0 : 1;
}
