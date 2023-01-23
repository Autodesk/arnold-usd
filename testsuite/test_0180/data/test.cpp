#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AiBegin();
    AiSceneLoad(nullptr, "scene.usda", nullptr);
    AiSceneWrite(nullptr, "scene.ass", nullptr, nullptr);
    AiEnd();
    AiBegin();
    AiSceneLoad(nullptr, "scene.ass", nullptr);
    AtNode *options = AiUniverseGetOptions(nullptr);
    AtArray *array = AiNodeGetArray(options, AtString("outputs"));
    bool success = false;
    
    static const AtString elem1("RGBA RGBA /Render/Products/Vars/rendervar1/filter /Render/Products/renderproduct1 beauty");
    static const AtString elem2("RGBA RGBA /Render/Products/Vars/rendervar2/filter /Render/Products/renderproduct1 beauty_filtered");
    if (AiArrayGetNumElements(array) == 2)
    {
        AtString val1 = AiArrayGetStr(array, 0);
        AtString val2 = AiArrayGetStr(array, 1);
        if (val1 == elem1 && val2 == elem2)
        {
            success = true;
        } else
        {
            AiMsgError("Wrong outputs : %s, %s", val1.c_str(), val2.c_str());
        }
    }
    if (success)
        AiRender(nullptr);
    AiEnd();
    
    return (success) ? 0 : 1;
}
