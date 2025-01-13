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

    AiSceneLoad(render_universe, "scene.ass", nullptr);
    AtUniverse *proc_universe = AiUniverse();
    AtNode *proc = AiNode(proc_universe, "usd", "usd_proc");
    AiNodeSetStr(proc, "filename", "nested_proc.usda");

    AiProceduralViewport(proc, render_universe, AI_PROC_POLYGONS);

    AtNode *proc2 = AiNode(proc_universe, "usd", "usd_proc2");
    AiNodeSetStr(proc2, "filename", "cube.usda");

    AiProceduralViewport(proc2, render_universe, AI_PROC_POLYGONS);

    AiUniverseDestroy(proc_universe);
    AiRender(render_session);
    AiRenderSessionDestroy(render_session);
    AiUniverseDestroy(render_universe);
    AiEnd();
    return 0;
}
