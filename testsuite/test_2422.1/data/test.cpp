#include <ai.h>

#include <cstdlib>
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
      AiMsgError("Procedural node not found");
      AiEnd();
      return -1;
   }

   AtRenderSession *render_session = AiRenderSession(universe, AI_SESSION_INTERACTIVE);
   // First render, at frame 1
   AiRender(render_session);

   // This material isn't animated, its node should be preserved when the frame changes
   const AtString material_name("/root/mtl/Material__43/Material__43");
   AtNode *material = AiNodeLookUpByName(universe, material_name, proc);

   // Change the frame, and start a second render
   AiNodeSetFlt(proc, AtString("frame"), 5);
   AiRender(render_session);

   const char *use_hydra = std::getenv("PROCEDURAL_USE_HYDRA");
   const bool hydra = (use_hydra == nullptr || std::string(use_hydra) != "0");
   int result = 0;
   if (material == nullptr || AiNodeLookUpByName(universe, material_name, proc) == nullptr)
   {
      AiMsgError("Material %s not found", material_name.c_str());
      result = -1;
   }
   else if (hydra && AiNodeLookUpByName(universe, material_name, proc) != material)
   {
      AiMsgError("The procedural was regenerated instead of being updated");
      result = -1;
   }

   AiRenderSessionDestroy(render_session);
   AiUniverseDestroy(universe);
   AiEnd();
   return result;
}
