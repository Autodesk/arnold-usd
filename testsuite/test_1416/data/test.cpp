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
    AiParamValueMapSetBool(params, AtString("convert_string_outputs"), false);
    AiSceneLoad(nullptr, "test.usda", params);
    AiSceneWrite(nullptr, "scene.ass", params);
    
    AiEnd();
    AiBegin();

    AiSceneLoad(nullptr, "scene.ass", params);
    AiParamValueMapDestroy(params);
    AtArray *array = AiNodeGetArray(AiUniverseGetOptions(nullptr), AtString("outputs"));
    AtString output1 = AiArrayGetStr(array, 0);
    AtString output2 = AiArrayGetStr(array, 1);
    AtString output3 = AiArrayGetStr(array, 2);

    const static AtString refOutput1("RGBA RGBA /Render/Vars/beauty/filter /Render/Products/outImg HALF");
    const static AtString refOutput2("albedo RGBA /Render/Vars/albedo/filter /Render/Products/outImg");
    const static AtString refOutput3("diffuse RGBA /Render/Vars/diffuse/filter /Render/Products/outImg my_diffuse HALF");

    AiEnd();
    if (output1 == refOutput1 && output2 == refOutput2 && output3 == refOutput3){
        return 0;
    }

    if (output1 != refOutput1)
        std::cerr<<"First output is different : "<<output1.c_str()<<" instead of "<<refOutput1.c_str()<<std::endl;

    if (output2 != refOutput2)
        std::cerr<<"Second output is different : "<<output2.c_str()<<" instead of "<<refOutput2.c_str()<<std::endl;

    if (output3 != refOutput3)
        std::cerr<<"Third output is different : "<<output3.c_str()<<" instead of "<<refOutput3.c_str()<<std::endl;

    return 1;
}







