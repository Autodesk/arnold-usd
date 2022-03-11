#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>

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

    AiBegin();
    bool success = true;
    AiSceneLoad(nullptr, "scene.usda", nullptr);
    
    if (!AiNodeLookUpByName("/beautiful/scope/my/sphere/name")) {
        AiMsgError("/beautiful/scope/my/sphere/name doesn't exist");
        success = false;
    }
    if (!AiNodeLookUpByName("/beautiful/scope/my/cube/name")) {
        AiMsgError("/beautiful/scope/my/cone/name doesn't exist");
        success = false;
    }
    if (!AiNodeLookUpByName("/beautiful/scope/lambert1/lambert1")) {
        AiMsgError("/beautiful/scope/lambert1 doesn't exist");
        success = false;
    }
    if (!AiNodeLookUpByName("/beautiful/scope/persp/perspShape")) {
        AiMsgError("/beautiful/scope/persp/perspShape doesn't exist");
        success = false;
    }
    if (!AiNodeLookUpByName("/beautiful/scope/defaultArnoldFilter_gaussian_filter")) {
        AiMsgError("/beautiful/scope/defaultArnoldFilter_gaussian_filter doesn't exist");
        success = false;
    }
    if (!AiNodeLookUpByName("/beautiful/scope/directionalLight1/directionalLightShape1")) {
        AiMsgError("/beautiful/scope/directionalLight1/directionalLightShape1 doesn't exist");
        success = false;
    }
        
    AiEnd();
    return (success) ? 0 : 1;
}
