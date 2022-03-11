#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(AI_LOG_ALL);
    AiBegin();
    AiASSLoad("scene.ass");
    AtParamValueMap* params = AiParamValueMap();
    AiParamValueMapSetStr(params, AtString("scope"), AtString("beautiful/scope"));
    AiSceneWrite(nullptr, "scene.usda", params);
    AiParamValueMapDestroy(params);
    AiEnd();

    std::vector<std::string> includeList = {"/aiStandard3/aiStandard3", 
                                            "/aiStandard3/checker",
                                            "/aiStandard3displacementShader1/aiStandard3",
                                            "/aiStandard3displacementShader1/checker",
                                            "/aiStandard3displacementShader1/displacementShader1",
                                            "/aiStandard3displacementShader1/bulge1",
                                            "/aiStandard3displacementShader2/aiStandard3",
                                            "/aiStandard3displacementShader2/checker",
                                            "/aiStandard3displacementShader2/displacementShader2",
                                            "/aiStandard3displacementShader2/noise1",
                                            "/aiStandard5displacementShader3/aiStandard5",
                                            "/aiStandard5displacementShader3/displacementShader3",
                                            "/aiStandard5displacementShader3/checker1_cc",
                                            "/aiStandard5displacementShader3/checker1",
                                            "/place2dTexture1_u",
                                            "/place2dTexture1_v",
                                            "/place2dTexture1",
                                            "/place2dTexture1_passthrough",
                                            "/place2dTexture2_u",
                                            "/place2dTexture2_v",
                                            "/place2dTexture2",
                                            "/place2dTexture2_passthrough",
                                        };

    AiBegin();
    bool success = true;
    AiSceneLoad(nullptr, "scene.usda", nullptr);

    for (auto &testName : includeList) {
        std::string name = "/beautiful/scope" + testName;
        if (!AiNodeLookUpByName(name.c_str())) {
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
        if (AiNodeLookUpByName(testName.c_str())) {
            success = false;
            AiMsgError("%s shouldn't exist", testName.c_str());
        }
        std::string name = "/beautiful/scope" + testName;
        if (AiNodeLookUpByName(name.c_str())) {
            success = false;
            AiMsgError("%s shouldn't exist", name.c_str());
        }
    }

    AiEnd();
    return (success) ? 0 : 1;
}







