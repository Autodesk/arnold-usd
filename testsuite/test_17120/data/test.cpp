#include <ai.h>

#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#define NUM_FRAMES 40

int main(int, char**)
{
   AiBegin(AI_SESSION_INTERACTIVE);
   AtUniverse* universe = AiUniverse();

   AtParamValueMap* params = AiParamValueMap();
   bool success = AiSceneLoad(universe, AtString("scene.ass"), params);
   AiParamValueMapDestroy(params);
   if (!success)
   {
      AiUniverseDestroy(universe);
      AiEnd();
      return -1;
   }
   // export to USD
   for (int i = 0; i < NUM_FRAMES; ++i)
   {
      AtParamValueMap* params = AiParamValueMap();
      AiParamValueMapSetBool(params, AtString("binary"), false);
      AiParamValueMapSetFlt(params, AtString("frame"), (float)i);
      AiParamValueMapSetBool(params, AtString("append"), i > 0);
      bool success = AiSceneWrite(universe, "test_resaved.usda", params);
      AiParamValueMapDestroy(params);

      if (!success)
      {
         printf("[ERROR] Scene export failed\n");
         AiUniverseDestroy(universe);
         AiEnd();
         return 1;
      }
   }
   AiUniverseDestroy(universe);
   std::ifstream file;
   std::string str;
   file.open("test_resaved.usda" , std::ios::binary | std::ios::in);
   if (!file.good()) {
      printf("Cannot open resaved file");
      AiEnd();
      return 1;
   }
   int numSubsets = 0;
   while(std::getline(file , str))
   {
      if (str.find("def GeomSubset ") != std::string::npos)
         numSubsets++;
   }
   if (numSubsets != 10)
   {
      printf("Wrong amount of subsets %d\n", numSubsets);
      AiEnd();
      return 1;
   }

   universe = AiUniverse();
   params = AiParamValueMap();
   success = AiSceneLoad(universe, AtString("test_resaved.usda"), params);
   if (!success)
   {
      printf("could not load resaved usd file");
      AiUniverseDestroy(universe);
      AiEnd();
      return -1;
   }
   AiParamValueMapDestroy(params);

   AtRenderSession *render_session = AiRenderSession(universe);
   AiRender(render_session);

   AiRenderSessionDestroy(render_session);
   AiUniverseDestroy(universe);
   AiEnd();
   return 0;
   
}
