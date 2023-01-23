#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AiBegin();

    AtUniverse *proc_universe = AiUniverse();

    AiSceneLoad(nullptr, "scene.ass", nullptr);
    // load the usd procedural (containing a sphere) in a separate universe
    AiSceneLoad(proc_universe, "usd_proc.ass", nullptr);
    AtNode *proc_a = AiNode(proc_universe, "usd", "usd_proc_a");
    AiNodeSetStr(proc_a, "filename", "usd_proc_a.usda");
    // Call the viewport API on the usd procedural, and fill the main universe
    AiProceduralViewport(proc_a, nullptr, AI_PROC_POLYGONS);
    AtNode *proc_b = AiNode(proc_universe, "usd", "usd_proc_b");
    AiNodeSetStr(proc_b, "filename", "usd_proc_b.usda");
    // Call the viewport API on the usd procedural, and fill the main universe
    AiProceduralViewport(proc_b, nullptr, AI_PROC_BOXES);

    AiUniverseDestroy(proc_universe);
    AiRender(nullptr);

    AiEnd();
    return 0;
}
