#include <ai.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

bool CheckUserData(const char *name)
{
    AtNode *node = AiNodeLookUpByName(nullptr, AtString(name));
    if (node == nullptr)
        return false;

    const AtString s_intAttr("one_value");
    const AtString s_fltAttr("two_value");
    const AtString s_colorAttr("color_value");
    const AtString s_intensityAttr("intensity");

    if (!AiNodeLookUpUserParameter (node, s_intAttr))
        return false;
    int intVal = AiNodeGetInt(node, s_intAttr);
    if (intVal != 1)
        return false;

    if (!AiNodeLookUpUserParameter (node, s_fltAttr))
        return false;
    float fltVal = AiNodeGetFlt(node, s_fltAttr);
    if (fltVal != 2)
        return false;

    if (!AiNodeLookUpUserParameter (node, s_colorAttr))
        return false;
    AtRGB colorVal = AiNodeGetRGB(node, s_colorAttr);
    static AtRGB blue(0.f, 0.f, 1.f);
    if (colorVal != blue)
        return false;

    float intensity = AiNodeGetFlt(node, s_intensityAttr);
    if (intensity != 0.5f)
        return false;


    return true;    
}

int main(int argc, char **argv)
{
    AiBegin(); 
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);
    AtParamValueMap* params = AiParamValueMap();
    AiSceneLoad(nullptr, "scene.usda", nullptr);
    AiSceneWrite(nullptr, "scene.ass", params);
    AiParamValueMapDestroy(params);
    AiEnd();

    AiBegin();
    AiSceneLoad(nullptr, "scene.ass", nullptr);

    bool success = true;
    AtNode *dome = AiNodeLookUpByName(nullptr, AtString("/dome"));

    success &= CheckUserData("/dome");
    success &= CheckUserData("/distant");
    success &= CheckUserData("/disk");
    success &= CheckUserData("/rect");
    success &= CheckUserData("/sphere");
    success &= CheckUserData("/cylinder");
    AiEnd();
    return (success) ? 0 : 1;
}







