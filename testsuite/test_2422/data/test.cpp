#include <ai.h>

#include <vector>
#include <string>

int main(int, char**)
{
   AiBegin(AI_SESSION_INTERACTIVE);
   AtUniverse* universe = AiUniverse();

   AtParamValueMap* params = AiParamValueMap();
   bool success = AiSceneLoad(universe, AtString("scene.usda"), params);
   AiParamValueMapDestroy(params);

   if (!success)
   {
      AiEnd();
      return -1;
   }

   AtNode *proc = AiNodeLookUpByName(universe, AtString("/proc"));
   if (!proc)
   {
      AiEnd();
      AiMsgError("Procedural node not found");
      return -1;
   }

   AtRenderSession *render_session = AiRenderSession(universe);
   // First render, at frame 1
   AiRender(render_session);

   // Change the frame, and start a second render
   AiNodeSetFlt(proc, AtString("frame"), 5);
   
   AiRender(render_session);
   AiRenderSessionDestroy(render_session);
   AiUniverseDestroy(universe);
   AiEnd();
   return 0;
   
}
