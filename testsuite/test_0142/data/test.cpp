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
    AtArray *array = AiArrayAllocate(1, 1, AI_TYPE_STRING);
    
    std::string overrides = "#usda 1.0\ndef \"pCube1\"\n{def Mesh \"pCubeShape1\"{\nuniform bool doubleSided = 1\n";
    overrides += "int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]\nint[] faceVertexIndices = [0, 1, 3, 2, 2, 3, 5, 4, 4, 5, 7, 6, 6, 7, 1, 0, 1, 7, 5, 3, 6, 0, 2, 4]\n";
    overrides += "rel material:binding = </materials/lambert1>\nuniform token orientation = \"rightHanded\"\n";
    overrides += "point3f[] points = [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (-0.5, 0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5), (-0.5, -0.5, -0.5), (0.5, -0.5, -0.5)]\n}\n}";
    AiArraySetStr(array, 0, overrides.c_str());
    AiNodeSetArray(proc, "overrides", array);

    AiProceduralViewport(proc, render_universe, AI_PROC_POLYGONS);

    AiUniverseDestroy(proc_universe);
    AiRender(render_session);
    AiRenderSessionDestroy(render_session);
    AiUniverseDestroy(render_universe);
    AiEnd();
    return 0;
}
