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
    AtParamValueMap* params = AiParamValueMap();
    AiSceneWrite(nullptr, "scene.usda", params);
    AiParamValueMapSetBool(params, AtString("all_attributes"), true);
    AiSceneWrite(nullptr, "scene2.usda", params);
    AiParamValueMapDestroy(params);

    int optionsAttrs = 0;
    AtParamIterator* nodeParam = 
            AiNodeEntryGetParamIterator(AiNodeGetNodeEntry(AiUniverseGetOptions(nullptr)));
    while (!AiParamIteratorFinished(nodeParam)) {
        AiParamIteratorGetNext(nodeParam);
        optionsAttrs++;
    }
    AiParamIteratorDestroy(nodeParam);

    AiEnd();

    int noDefaultCount = 0;
    std::ifstream file("scene.usda");
    if (file.is_open())
    {
        std::string line;
        while(std::getline(file, line))
        {
            if (line.find(" arnold:") != std::string::npos)
                noDefaultCount++;
        }
    }

    int withDefaultCount = 0;
    std::ifstream file2("scene2.usda");
    if (file2.is_open())
    {
        std::string line;
        bool countAttrs = false; 
        while(std::getline(file2, line))
        {
            if (line.find("def RenderSettings ") != std::string::npos)
                countAttrs = true;
            else if (line.substr(0, 4) == std::string("def "))
                countAttrs = false;

            if (countAttrs && line.find(" arnold:") != std::string::npos)
                withDefaultCount++;        
        }
    }
    file2.close();
    bool success = true;
    if (noDefaultCount > 5) {
        AiMsgError("Too many attribute saved by default : found %d", noDefaultCount);
        success = false;
    }
    const static int skippedAttrs = 17; // Some attributes should still be skipped
    
    if (withDefaultCount + skippedAttrs < optionsAttrs) {
        AiMsgError("Mismatch in attributes count with all_attributes enabled. Found %d, expected %d", withDefaultCount, optionsAttrs - skippedAttrs);
        success = false;
    }

    return (success) ? 0 : 1;
}
