#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AiBegin();

    AtUniverse *proc_universe = AiUniverse();
    AtUniverse *render_universe = AiUniverse();
    AtRenderSession *render_session = AiRenderSession(render_universe);

    AiSceneLoad(render_universe, "scene.ass", nullptr);
    // load the usd procedural (containing a sphere) in a separate universe
    AtNode *proc_a = AiNode(proc_universe, "usd", "usd_proc_a");
    AiNodeSetStr(proc_a, "filename", "usd_proc_a.usda");
    // Call the viewport API on the usd procedural, and fill the main universe
    AiProceduralViewport(proc_a, render_universe, AI_PROC_POLYGONS);
    AtNode *proc_b = AiNode(proc_universe, "usd", "usd_proc_b");
    AiNodeSetStr(proc_b, "filename", "usd_proc_b.usda");
    // Call the viewport API on the usd procedural, and fill the main universe
    AiProceduralViewport(proc_b, render_universe, AI_PROC_BOXES);

    AiUniverseDestroy(proc_universe);
    AiRender(render_session);

    AiRenderSessionDestroy(render_session);
    AiUniverseDestroy(render_universe);

    AiEnd();
    return 0;
}
