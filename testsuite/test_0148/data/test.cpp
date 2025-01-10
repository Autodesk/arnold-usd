#include <ai.h>

#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AiBegin();
    AiSceneLoad(nullptr, AtString("scene.ass"), nullptr);
    AiSceneWrite(nullptr, AtString("scene_exported.usda"), nullptr);
    
    AtUniverse *universe = AiUniverse();
    AiSceneLoad(universe, AtString("scene_exported.usda"), nullptr);
    AtNode *mesh = AiNodeLookUpByName(universe, AtString("/root/world/geo/primitive"));
    if (mesh) {
        AtArray *nsides = AiNodeGetArray(mesh, AtString("nsides"));
        if (nsides && AiArrayGetNumElements(nsides) == 2) {
            AiEnd();
            return 0;
        }
    }
    AiEnd();
    return 1;
}
