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
    AiNodeSetStr(proc_a, "filename", "attributes.usda");
    // Call the viewport API on the usd procedural, and fill the main universe
    AiProceduralViewport(proc_a, render_universe, AI_PROC_BOXES);
    AiProceduralViewport(proc_a, render_universe, AI_PROC_POLYGONS);

    AiUniverseDestroy(proc_universe);
    AiRender(render_session);
    AtParamValueMap* params = AiParamValueMap();
    AiParamValueMapSetInt(params, AtString("binary"), false);
    AiSceneWrite(render_universe, "test_resaved.ass", params);
    AiParamValueMapDestroy(params);

    AiRenderSessionDestroy(render_session);
    AiUniverseDestroy(render_universe);

    AiEnd();
    return 0;
}
