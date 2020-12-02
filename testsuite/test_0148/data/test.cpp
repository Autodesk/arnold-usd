#include <ai.h>

#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(AI_LOG_ALL);
    AiBegin();
    AiSceneLoad(nullptr, "scene.ass", nullptr);
    AiSceneWrite(nullptr, "scene_exported.usda", nullptr);
    
    AtUniverse *universe = AiUniverse();
    AiSceneLoad(universe, "scene_exported.usda", nullptr);
    AtNode *mesh = AiNodeLookUpByName(universe, "/root/world/geo/primitive");
    if (mesh) {
        AtArray *nsides = AiNodeGetArray(mesh, "nsides");
        if (nsides && AiArrayGetNumElements(nsides) == 2) {
            AiEnd();
            return 0;
        }
    }
    AiEnd();
    return 1;
}
