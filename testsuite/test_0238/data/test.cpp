#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(AI_LOG_ALL);
    AiBegin();
    AtParamValueMap* params = AiParamValueMap();
    AiASSLoad("test.usda");
    AiSceneWrite(nullptr, "scene.ass", params);
    AiParamValueMapDestroy(params);
    AiEnd();

    AiBegin();
    AiSceneLoad(nullptr, "scene.ass", nullptr);
    AtArray *array = AiNodeGetArray(AiUniverseGetOptions(nullptr), AtString("outputs"));
    AtString output1 = AiArrayGetStr(array, 0);
    AtString output2 = AiArrayGetStr(array, 1);

    const static AtString refOutput1("RGBA RGBA /Render/Vars/beauty/filter /Render/Products/beauty");
    const static AtString refOutput2("RGBA RGBA /Render/Vars/beauty/filter /Render/Products/beauty2");

    AiEnd();
    if (output1 == refOutput1 && output2 == refOutput2){
        return 0;
    }

    if (output1 != refOutput1)
        std::cerr<<"First output is different : "<<output1.c_str()<<" instead of "<<refOutput1.c_str()<<std::endl;

    if (output2 != refOutput2)
        std::cerr<<"Second output is different : "<<output2.c_str()<<" instead of "<<refOutput2.c_str()<<std::endl;

    return 1;
}







