#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(AI_LOG_ALL);
    AiBegin();

    AiASSLoad("scene.ass");
    AtUniverse *proc_universe = AiUniverse();
    AtNode *proc = AiNode(proc_universe, "usd", "usd_proc");
    AiNodeSetStr(proc, "filename", "wrong_transform_inheritance.usda");

    AiProceduralViewport(proc, nullptr, AI_PROC_POLYGONS);

    AtNode *proc2 = AiNode(proc_universe, "usd", "usd_proc2");
    AiNodeSetStr(proc2, "filename", "correct_transform_inheritance.usda");

    AiProceduralViewport(proc2, nullptr, AI_PROC_POLYGONS);

    AiUniverseDestroy(proc_universe);
    AiRender();

    AiEnd();
    return 0;
}
