#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    AiBegin(); 
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AtParamValueMap* params = AiParamValueMap();
    AiSceneLoad(nullptr, "scene.ass", nullptr);
    AiParamValueMapSetStr(params, AtString("mtl_scope"), AtString("Looks"));
    AiSceneWrite(nullptr, "scene.usda", params);
    AiParamValueMapDestroy(params);
    AiEnd();

    AiBegin();
    AiSceneLoad(nullptr, "scene.usda", nullptr);

    const static AtString shaderName("/Looks/test/aiStandardSurface1/aiStandardSurface1");
    AtNode *shader = AiNodeLookUpByName(nullptr, shaderName);
    const static AtString noiseName("/Looks/test/aiStandardSurface1/my_noise");
    AtNode *noise = AiNodeLookUpByName(nullptr, noiseName);
    bool success = (shader != nullptr && noise != nullptr);
    AiEnd();

    return (success) ? 0 : 1;
}







