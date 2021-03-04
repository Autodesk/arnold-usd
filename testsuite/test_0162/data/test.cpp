#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(AI_LOG_ALL);
    AiBegin();

    AtParamValueMap *params = AiParamValueMap();
    AiParamValueMapSetBool(params, AtString("procedurals_only"), true);    

    AiASSLoad("scene.ass");
    AtUniverse *proc_universe = AiUniverse();
    AtNode *proc = AiNode(proc_universe, "usd", "usd_proc");
    AiNodeSetStr(proc, "filename", "nested_proc.usda");

    AiProceduralViewport(proc, nullptr, AI_PROC_POLYGONS, params);

    AiUniverseDestroy(proc_universe);
    AiRender();

    AiEnd();
    return 0;
}
