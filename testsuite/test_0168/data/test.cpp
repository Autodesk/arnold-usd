#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>

int main(int argc, char **argv)
{
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AiBegin();

    AtUniverse *render_universe = AiUniverse();
    AtRenderSession *render_session = AiRenderSession(render_universe);

    AtUniverse *proc_universe = AiUniverse();

    AiSceneLoad(render_universe, "scene.ass", nullptr);
    // load the usd procedural (containing a sphere) in a separate universe
    AtNode *proc_a = AiNode(proc_universe, "usd", "my_usd");
    AiNodeSetStr(proc_a, "filename", "scene.usda");
    // Call the viewport API on the usd procedural, and fill the main universe
    AiProceduralViewport(proc_a, render_universe, AI_PROC_POLYGONS);

    AiUniverseDestroy(proc_universe);
    AiNodeSetBool(AiUniverseGetOptions(render_universe), AtString("abort_on_license_fail"), false);
    AiRender(render_session);

    AiRenderSessionDestroy(render_session);
    AiUniverseDestroy(render_universe);
    AiEnd();
    return 0;
}
