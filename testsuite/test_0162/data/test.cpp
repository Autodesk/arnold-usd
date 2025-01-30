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

    AtParamValueMap *params = AiParamValueMap();
    AiParamValueMapSetBool(params, AtString("procedurals_only"), true);    

    AiSceneLoad(render_universe, "scene.ass", nullptr);
    AtUniverse *proc_universe = AiUniverse();
    AtNode *proc = AiNode(proc_universe, "usd", "usd_proc");
    AiNodeSetStr(proc, AtString("filename"), AtString("nested_proc.usda"));

    AiProceduralViewport(proc, render_universe, AI_PROC_POINTS, params);

    AiUniverseDestroy(proc_universe);
    AiRender(render_session);
    AiRenderSessionDestroy(render_session);
    AiUniverseDestroy(render_universe);
    AiEnd();
    return 0;
}
