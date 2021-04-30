#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(AI_LOG_ALL);
    AiBegin();
    AtUniverse *universe1 = AiUniverse();
    AiSceneLoad(universe1, "scene.01.ass", nullptr);
    AtParamValueMap* params = AiParamValueMap();
    AiParamValueMapSetFlt(params, AtString("frame"), 1.f);    
    AiSceneWrite(universe1, "scene_exported.usda", params);
    AiUniverseDestroy(universe1);

    AtUniverse *universe2 = AiUniverse();
    AiSceneLoad(universe2, "scene.02.ass", nullptr);
    AiParamValueMapSetFlt(params, AtString("frame"), 2.f);    
    AiParamValueMapSetBool(params, AtString("append"), true);    
    AiSceneWrite(universe2, "scene_exported.usda", params);
    AiUniverseDestroy(universe2);

    AtUniverse *universe3 = AiUniverse();
    AiSceneLoad(universe3, "scene.03.ass", nullptr);
    AiParamValueMapSetFlt(params, AtString("frame"), 3.f);    
    AiParamValueMapSetBool(params, AtString("append"), true);
    AiSceneWrite(universe3, "scene_exported.usda", params);
    AiUniverseDestroy(universe3);

    AiParamValueMapDestroy(params);
    bool success = true;
    for (int i = 1; i <= 3; ++i) {
        AtUniverse *universe = AiUniverse();
        params = AiParamValueMap();
        AiParamValueMapSetFlt(params, AtString("frame"), (float) i);
        AiSceneLoad(universe, "scene_exported.usda", params);
        AtNode *light = AiNodeLookUpByName(universe, "/aiSkyDomeLight1");
        if (light == nullptr) {
            AiMsgError("Could not find /aiSkyDomeLight1");
            success = false;
            break;
        }

        if (AiNodeGetFlt(light, "exposure") != (float)i) {
            success = false;
            AiMsgError("Exposure value isn't correct for frame %d : found %d instead of %d", i, AiNodeGetFlt(light, "exposure"), i);
            break;
        }
        AiParamValueMapDestroy(params);
        AiUniverseDestroy(universe);
    }   
        
    AiEnd();
    return success ? 0 : 1;
}
