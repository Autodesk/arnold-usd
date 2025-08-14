#include <ai.h>

#include <vector>
#include <string>

//#define LOG_FLAGS AI_LOG_ALL
#define LOG_FLAGS AI_LOG_WARNINGS | AI_LOG_ERRORS

bool IsGeometrySkipped(AtUniverse* universe, const std::string& shape)
{
   AtNode *node = AiNodeLookUpByName(universe, AtString(shape.c_str()));
   if (node == nullptr)
   {
      AiMsgError("shape not found %s", shape.c_str());
      return false;
   }

   if (AiNodeIs(node, AtString("polymesh"))) {
      ////// Test if Mesh topology was translated to arnold, or properly skipped
      AtArray* vlist = AiNodeGetArray(node, AtString("vlist"));
      AtArray* nsides = AiNodeGetArray(node, AtString("nsides"));
      int vlistSize = vlist ? AiArrayGetNumElements(vlist) : 0;
      int nsidesSize = nsides ? AiArrayGetNumElements(nsides) : 0;

      if (vlistSize > 0 || nsidesSize > 0)
      {
         AiMsgError("%s was not properly skipped from translation", shape.c_str());
         return false;
      }
      return true;
   }

   if (AiNodeIs(node, AtString("curves")) || AiNodeIs(node, AtString("points"))) {
      ////// Test if Curves topology was translated to arnold, or properly skipped
      AtArray* points = AiNodeGetArray(node, AtString("points"));
      AtArray* radius = AiNodeGetArray(node, AtString("radius"));
      int pointsSize = points ? AiArrayGetNumElements(points) : 0;
      int radiusSize = radius ? AiArrayGetNumElements(radius) : 0;

      if (pointsSize > 0 || radiusSize > 0)
      {
         AiMsgError("%s was not properly skipped from translation", shape.c_str());
         return false;
      }
      return true;
   }
   return true;
}
int main(int, char**)
{
   AiBegin(AI_SESSION_INTERACTIVE);
   AtUniverse* universe = AiUniverse();

   AiMsgSetConsoleFlags(nullptr, LOG_FLAGS);

   AtParamValueMap* params = AiParamValueMap();
   bool success = AiSceneLoad(universe, AtString("scene.usda"), params);
   AiParamValueMapDestroy(params);

   if (!success)
   {
      AiEnd();
      return -1;
   }

   std::vector<std::string> shapes = {"/root/source/asset/mesh", 
                                      "/root/source/asset/curves",
                                      "/root/source/asset/points"};
   
   for (const auto& shape : shapes)
      success |= IsGeometrySkipped(universe, shape);

   return success ? 0 : -1;
}
