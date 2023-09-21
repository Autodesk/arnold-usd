#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

/// Testing animated parameter export using skydome_light.exposure.
/// @param samples List of samples to test.
/// @param filename USD filename to use.
bool testParameter(const std::vector<float>& samples, const std::string& filename)
{
    const AtString nodeName {"/aiSkyDomeLight1"};
    const AtString paramName {"exposure"};
    float frame = 1.0f;
    
    for (const auto& sample: samples) {
        auto* universe = AiUniverse();
        auto* light = AiNode(universe, AtString{"skydome_light"});
        AiNodeSetFlt(light, paramName, sample);
        AiNodeSetStr(light, AtString{"name"}, nodeName);
        auto* params = AiParamValueMap();
        AiParamValueMapSetFlt(params, AtString{"frame"}, frame);
        if (frame > 1.0f) {
            AiParamValueMapSetBool(params, AtString{"append"}, true);
        }
        AiSceneWrite(universe, filename.c_str(), params);
        AiParamValueMapDestroy(params);
        AiUniverseDestroy(universe);
        frame += 1.0f;
    }

    bool success = true;
    frame = 1.0f;
    for (const auto& sample: samples) {
        auto* universe = AiUniverse();
        auto* params = AiParamValueMap();
        AiParamValueMapSetFlt(params, AtString("frame"), frame);
        AiSceneLoad(universe, filename.c_str(), params);
        auto* light = AiNodeLookUpByName(universe, nodeName);
        if (light == nullptr) {
            AiMsgError("Could not find %s", nodeName.c_str());
            AiParamValueMapDestroy(params);
            AiUniverseDestroy(universe);
            success = false;
            break;
        }
        float paramVal = AiNodeGetFlt(light, paramName);
        if (std::abs(paramVal - sample) > AI_EPSILON ) {
            AiMsgError("%s value isn't correct for frame %d : found %d instead of %d",
                       paramName.c_str(), frame, paramVal, sample);
            success = false;
        }
        AiParamValueMapDestroy(params);
        AiUniverseDestroy(universe);
        frame += 1.0f;
    }

    return success;
}

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AiBegin();

    bool sucess =
        testParameter({1.0f, 2.0f, 3.0f, 4.0f}, "increasing_samples.usda") &&
        testParameter({1.0f, 2.0f, 2.0f, 2.0f, 2.0f, 1.0f, 2.0f}, "wavy_samples.usda") &&
        testParameter({1.0f, 1.0f, 1.0f, 2.0f}, "flat_start.usda");
    AiEnd();

    return sucess ? 0 : 1;
}
