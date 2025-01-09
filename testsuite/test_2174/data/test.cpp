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
    AtUniverse *universe = AiUniverse();
    AiSceneLoad(universe, "scene.ass", nullptr);

    AtParamValueMap* params = AiParamValueMap();
    AiParamValueMapSetInt(params, AtString("mask"), AI_NODE_SHADER);
    AiSceneWrite(universe, "scene.usda", params);
    AiParamValueMapDestroy(params);
    AiEnd();
    bool success = false;

    bool foundMtlScope = false;
    std::ifstream file("scene.usda");
    if (file.is_open())
    {
        std::string line;
        while(std::getline(file, line))
        {
            if (line.find("def Scope \"mtl\"") != std::string::npos)
                foundMtlScope = true;
            else if (foundMtlScope && line.find("def Shader \"") != std::string::npos) {
                success = true;
                break;
            }
        }
    }

    return (success) ? 0 : 1;
}
