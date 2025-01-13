#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    AiBegin();
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AiSceneLoad(nullptr, "scene.ass", nullptr);
    AtParamValueMap* params = AiParamValueMap();
    AiParamValueMapSetStr(params, AtString("scope"), AtString("beautiful/scope"));
    AiSceneWrite(nullptr, "scene.usda", params);
    AiParamValueMapDestroy(params);
    AiEnd();

    std::vector<std::string> includeList = {"/mtl/aiStandard3/aiStandard3", 
                                            "/mtl/aiStandard3/checker",
                                            "/mtl/aiStandard3displacementShader1/aiStandard3",
                                            "/mtl/aiStandard3displacementShader1/checker",
                                            "/mtl/aiStandard3displacementShader1/displacementShader1",
                                            "/mtl/aiStandard3displacementShader1/bulge1",
                                            "/mtl/aiStandard3displacementShader2/aiStandard3",
                                            "/mtl/aiStandard3displacementShader2/checker",
                                            "/mtl/aiStandard3displacementShader2/displacementShader2",
                                            "/mtl/aiStandard3displacementShader2/noise1",
                                            "/mtl/aiStandard5displacementShader3/aiStandard5",
                                            "/mtl/aiStandard5displacementShader3/displacementShader3",
                                            "/mtl/aiStandard5displacementShader3/checker1_cc",
                                            "/mtl/aiStandard5displacementShader3/checker1",
                                            "/mtl/aiStandard3/place2dTexture1_u",
                                            "/mtl/aiStandard3/place2dTexture1_v",
                                            "/mtl/aiStandard3/place2dTexture1",
                                            "/mtl/aiStandard3/place2dTexture1_passthrough",
                                            "/mtl/aiStandard5/place2dTexture2_u",
                                            "/mtl/aiStandard5/place2dTexture2_v",
                                            "/mtl/aiStandard5/place2dTexture2",
                                            "/mtl/aiStandard5/place2dTexture2_passthrough",
                                        };

    AiBegin();
    bool success = true;
    AiSceneLoad(nullptr, "scene.usda", nullptr);

    for (auto &testName : includeList) {
        std::string name = "/beautiful/scope" + testName;
        if (!AiNodeLookUpByName(nullptr, name.c_str())) {
            success = false;
            AiMsgError("%s doesn't exist", name.c_str());
        }
    }
    std::vector<std::string> excludeList = {"/checker",
                                            "/checker1",
                                            "/bulge1",
                                            "/displacementShader1",
                                            "/displacementShader2",
                                            "/displacementShader3",
                                            "/noise1",
                                            "/checker1_cc" };
    for (auto &testName : excludeList) {
        if (AiNodeLookUpByName(nullptr, testName.c_str())) {
            success = false;
            AiMsgError("%s shouldn't exist", testName.c_str());
        }
        std::string name = "/beautiful/scope" + testName;
        if (AiNodeLookUpByName(nullptr, name.c_str())) {
            success = false;
            AiMsgError("%s shouldn't exist", name.c_str());
        }
    }

    AiEnd();
    return (success) ? 0 : 1;
}







