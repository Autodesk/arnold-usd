#include <ai.h>

#include <stdio.h>

#define SCENE_FILE "scene.usda"

int save_scene()
{
    int errorCode = 0;
    AtUniverse* universe = AiUniverse();
    /* create node */
    AtNode* imager = AiNode(universe, AtString("imager_color_curves"), AtString("myimager"));
    /* declare user array parameter */
    AiNodeDeclare(imager, AtString("my_array"), AtString("constant ARRAY FLOAT")); // Matrice ????
    /* set user array parameter */
    AtArray* values = AiArray(16, 1, AI_TYPE_FLOAT);
    for (int i = 0; i < 16; i++)
        AiArraySetFlt(values, i, (float)i);
    AiNodeSetArray(imager, AtString("my_array"), values);

    /* save to file */
    AtParamValueMap* params = AiParamValueMap();
    if (!AiSceneWrite(universe, SCENE_FILE, params, NULL)) {
        printf("[ERROR] Unable to write " SCENE_FILE "\n");
        errorCode = 1;
    }
    AiParamValueMapDestroy(params);

    AiUniverseDestroy(universe);
    return errorCode;
}

int load_scene()
{
    int errorCode = 0;
    AtUniverse* universe = AiUniverse();
    AtNode* imager = NULL;
    AtArray* values = NULL;

    /* load from file */
    AtParamValueMap* params = AiParamValueMap();
    if (!AiSceneLoad(universe, SCENE_FILE, params)) {
        printf("[ERROR] Failed to read " SCENE_FILE "\n");
        errorCode = 1;
        AiParamValueMapDestroy(params);
        goto release_universe;
    }
    AiSceneWrite(universe, "scene.ass", params, NULL);
    AiParamValueMapDestroy(params);

    /* verify */
    imager = AiNodeLookUpByName(universe, AtString("myimager"));
    if (imager == nullptr) {
        printf("[ERROR] 'myimager' nodes is not found\n");
        errorCode = 1;
        goto release_universe;
    }
    if (AiNodeLookUpUserParameter(imager, AtString("my_array")) == nullptr) {
        printf("[ERROR] 'my_array' user parameter is not found\n");
        errorCode = 1;
        goto release_universe;
    }
    values = AiNodeGetArray(imager, AtString("my_array"));
    if (values == nullptr) {
        printf("[ERROR] 'my_array' is defined null\n");
        errorCode = 1;
        goto release_universe;
    }
    if (AiArrayGetNumElements(values) != 16) {
        printf("[ERROR] 'my_array' has incorrect number of elements: %u\n", AiArrayGetNumElements(values));
        errorCode = 1;
        goto release_universe;
    }
    for (int i = 0; i < 16; i++) {
        float v = AiArrayGetFlt(values, i);
        if (v != (float)i) {
            printf("[ERROR] 'my_array' %d. element is incorrect: %f\n", v);
            errorCode = 1;
            goto release_universe;
        }
    }
release_universe:
    AiUniverseDestroy(universe);
    return errorCode;
}

int main(int argc, const char** argv)
{
    int errorCode = 0;
    AiBegin(AI_SESSION_BATCH);
    AiMsgSetConsoleFlags(nullptr, AI_LOG_WARNINGS | AI_LOG_ERRORS | AI_LOG_BACKTRACE);
    errorCode += save_scene();
    errorCode += load_scene();
    AiEnd();
    return errorCode;
}
