#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char **argv)
{
    bool success = true;
    AiBegin();
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AtString renderSettingsParam("render_settings");
    for (int i = 0; i <= 4; ++i) {
        AtUniverse* universe = AiUniverse();
        AtParamValueMap* params = AiParamValueMap();
        int index = AiMax(i, 1);
        std::string indexStr = std::to_string(index);
        std::string renderSettingsName = "/Render/rendersettings" + indexStr;
        if (i > 0)
            AiParamValueMapSetStr(params, renderSettingsParam, AtString(renderSettingsName.c_str()));
        AiSceneLoad(universe, "scene.usda", params);    
        AiParamValueMapDestroy(params);

        AtNode *options = AiUniverseGetOptions(universe);
        int AA_samples = AiNodeGetInt(options, AtString("AA_samples"));
        int GI_diffuse_depth = AiNodeGetInt(options, AtString("GI_diffuse_depth"));
        int GI_diffuse_samples = AiNodeGetInt(options, AtString("GI_diffuse_samples"));
        int GI_specular_depth = AiNodeGetInt(options, AtString("GI_specular_depth"));
        AtNode *camera = AiUniverseGetCamera(universe);
        std::string cameraName;
        if (camera) {
            cameraName = AiNodeGetName(camera);
        }
        std::string expectedCameraName = std::string("/cameras/camera") + indexStr;

        if (AA_samples != index) {
            std::cerr<<"For "<<renderSettingsName<<", wrong attribute AA_samples = "<<AA_samples<<std::endl;
            success = false;
        }
        if (GI_diffuse_depth != index) {
            std::cerr<<"For "<<renderSettingsName<<", wrong attribute GI_diffuse_depth = "<<GI_diffuse_depth<<std::endl;
            success = false;
        }
        if (GI_diffuse_samples != index) {
            std::cerr<<"For "<<renderSettingsName<<", wrong attribute GI_diffuse_samples = "<<GI_diffuse_samples<<std::endl;
            success = false;
        }
        if (GI_specular_depth != index) {
            std::cerr<<"For "<<renderSettingsName<<", wrong attribute GI_specular_depth = "<<GI_specular_depth<<std::endl;
            success = false;
        }
        if (cameraName != expectedCameraName) {
            std::cerr<<"For "<<renderSettingsName<<", wrong camera = "<<cameraName<<std::endl;
            success = false;
        }
        AiUniverseDestroy(universe);
    }
    AiEnd();
    return (success) ? 0 : 1;
}
