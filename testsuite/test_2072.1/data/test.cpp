#include <ai.h>

//#define LOG_FLAGS AI_LOG_ALL
#define LOG_FLAGS AI_LOG_WARNINGS | AI_LOG_ERRORS

int imager_export(const char* path)
{
   AiBegin(AI_SESSION_INTERACTIVE);
   AtUniverse* universe = AiUniverse();
   AiMsgSetConsoleFlags(universe, LOG_FLAGS);

   // create scene
   {
      AtNode* driver = AiNode(universe, AtString("driver_jpeg"), AtString("mydriver"));
      AtNode* filter = AiNode(universe, AtString("gaussian_filter"), AtString("myfilter"));
      AtNode* options = AiUniverseGetOptions(universe);
      AtArray* outputs = AiArrayAllocate(1, 1, AI_TYPE_STRING);
      AiArraySetStr(outputs, 0, "RGBA RGBA myfilter mydriver");
      AiNodeSetArray(options, AtString("outputs"), outputs);

      AtNode* color_correct = AiNode(universe, AtString("imager_color_correct"), AtString("imager1"));
      AtNode* exposure = AiNode(universe, AtString("imager_exposure"), AtString("imager2"));
      AtNode* tonemap = AiNode(universe, AtString("imager_tonemap"), AtString("imager3"));

      AiNodeSetPtr(exposure, AtString("input"), tonemap);
      AiNodeSetPtr(color_correct, AtString("input"), exposure);
      AiNodeSetPtr(driver, AtString("input"), color_correct);
   }

   // export imagers
   AtParamValueMap* params = AiParamValueMap();
   AiParamValueMapSetInt(params, AtString("mask"), AI_NODE_IMAGER|AI_NODE_DRIVER);
   bool success = AiSceneWrite(universe, path, params);
   AiParamValueMapDestroy(params);

   if (!success)
   {
      AiEnd();
      return -1;
   }

   AiEnd();
   printf("exported 3 imagers\n");
   return 3;
}

int imager_import(const char* path)
{
   AiBegin(AI_SESSION_INTERACTIVE);
   AtUniverse* universe = AiUniverse();
   AiMsgSetConsoleFlags(universe, LOG_FLAGS);

   AtParamValueMap* params = AiParamValueMap();
   AiParamValueMapSetInt(params, AtString("mask"), AI_NODE_IMAGER);
   bool success = AiSceneLoad(universe, path, params);
   AiParamValueMapDestroy(params);

   if (!success)
   {
      AiEnd();
      return -1;
   }

   // collect the imager nodes
   int numImagers = 0;
   AtNodeIterator* aiNodeIterator = AiUniverseGetNodeIterator(universe, AI_NODE_IMAGER);
   while (!AiNodeIteratorFinished(aiNodeIterator))
   {
      AtNode* imager = AiNodeIteratorGetNext(aiNodeIterator);
      printf(" imported %s (%s)\n", AiNodeGetName(imager), AiNodeEntryGetName(AiNodeGetNodeEntry(imager)));
      numImagers++;
   }
   AiNodeIteratorDestroy(aiNodeIterator);

   AiEnd();

   printf("read %d imagers\n", numImagers);
   return numImagers;
}

int main(int, char**)
{
   AiMsgSetConsoleFlags(nullptr, LOG_FLAGS);

   printf("IMAGER EXPORT\n");
   int exportedImagers = imager_export("imager_test.usda");
   if (!exportedImagers)
   {
      printf("[ERROR] Failed to export imagers\n");
      return 1;
   }

   printf("\n");

   printf("IMAGER IMPORT\n");
   int importedImagers = imager_import("imager_test.usda");
	if (importedImagers != exportedImagers)
   {
      printf("[ERROR] Failed to import imagers %d %d\n", importedImagers, exportedImagers);
      return 1;
   }

   return 0;
}
