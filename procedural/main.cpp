// Copyright 2019 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ai.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "reader.h"

#if defined(_DARWIN)
#include <dlfcn.h>
#endif

//-*************************************************************************
// Code for the Arnold procedural node loading USD files

AI_PROCEDURAL_NODE_EXPORT_METHODS(UsdProceduralMethods);

node_parameters
{
    AiParameterStr("filename", "");
    AiParameterStr("object_path", "");
    AiParameterFlt("frame", 0.0);
    AiParameterBool("debug", false);
    AiParameterInt("threads", 1);
    AiParameterArray("overrides", AiArray(0, 1, AI_TYPE_STRING));
}

procedural_init
{
    UsdArnoldReader *data = new UsdArnoldReader();
    *user_ptr = data;

    std::string filename(AiNodeGetStr(node, "filename"));
    if (filename.empty()) {
        return false;
    }
    std::string objectPath(AiNodeGetStr(node, "object_path"));
    data->setProceduralParent(node);
    data->setFrame(AiNodeGetFlt(node, "frame"));
    data->setDebug(AiNodeGetBool(node, "debug"));
    data->setThreadCount(AiNodeGetInt(node, "threads"));

    AtNode *renderCam = AiUniverseGetCamera();
    if (renderCam &&
        (AiNodeGetFlt(renderCam, AtString("shutter_start")) < AiNodeGetFlt(renderCam, AtString("shutter_end")))) {
        float motion_start = AiNodeGetFlt(renderCam, AtString("shutter_start"));
        float motion_end = AiNodeGetFlt(renderCam, AtString("shutter_end"));
        data->setMotionBlur((motion_start < motion_end), motion_start, motion_end);
    } else {
        data->setMotionBlur(false);
    }

    // export the USD file
    data->read(filename, AiNodeGetArray(node, "overrides"), objectPath);
    return 1;
}

//-*************************************************************************

procedural_cleanup
{
    delete reinterpret_cast<UsdArnoldReader *>(user_ptr);
    return 1;
}

//-*************************************************************************

procedural_num_nodes
{
    UsdArnoldReader *data = reinterpret_cast<UsdArnoldReader *>(user_ptr);
    if (data) {
        return data->getNodes().size();
    }
    return 0;
}

//-*************************************************************************

procedural_get_node
{
    UsdArnoldReader *data = reinterpret_cast<UsdArnoldReader *>(user_ptr);
    if (data) {
        return data->getNodes()[i];
    }
    return NULL;
}

#if defined(_DARWIN)
std::string USDLibraryPath()
{
   Dl_info info;
   if (dladdr("USDLibraryPath", &info))
   {
      std::string path = info.dli_fname;
      return path;
   }

   return std::string();
}
#endif

node_loader
{
    if (i > 0) {
        return false;
    }

    node->methods = UsdProceduralMethods;
    node->output_type = AI_TYPE_NONE;
    node->name = AtString("usd");
    node->node_type = AI_NODE_SHAPE_PROCEDURAL;
    strcpy(node->version, AI_VERSION);

    /* Fix the pre-10.13 OSX crashes at shutdown (#8866). Manually dlopening usd
    * prevents it from being unloaded since loads are reference counted
    * see : https://github.com/openssl/openssl/issues/653#issuecomment-206343347
    *       https://github.com/jemalloc/jemalloc/issues/1122
    */
#if defined(_DARWIN)
    const auto result = dlopen(USDLibraryPath().c_str(), RTLD_LAZY | RTLD_GLOBAL | RTLD_NODELETE);
    if (!result)
       AiMsgWarning("[USD] failed to re-load usd_proc.dylib. Crashes might happen on pre-10.13 OSX systems: %s\n", dlerror());
#endif
    return true;
}
