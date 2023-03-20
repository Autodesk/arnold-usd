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
    AiSceneWrite(nullptr, "scene.usda", params);
    AiParamValueMapDestroy(params);
    AiEnd();

    AiBegin();
    AiSceneLoad(nullptr, "scene.usda", nullptr);

    const static AtString shaderName("/test/aiStandardSurface1/aiStandardSurface1");
    AtNode *shader = AiNodeLookUpByName(nullptr, shaderName);
    bool success = (shader != nullptr);
    AiEnd();

    return (success) ? 0 : 1;

}







