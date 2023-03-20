#include <ai.h>

#include <stdio.h>

int main(int argc, const char** argv)
{
    int errorCode = 0;
    AiBegin(AI_SESSION_BATCH);
    AiMsgSetConsoleFlags(nullptr, AI_LOG_WARNINGS | AI_LOG_ERRORS | AI_LOG_BACKTRACE);

    AtUniverse* universe = AiUniverse();

    AtParamValueMap* params = AiParamValueMap();
    if (!AiSceneLoad(universe, "scene.usd", params)) {
        printf("[ERROR] Failed to read scene.usd\n");
        errorCode++;
        AiParamValueMapDestroy(params);
    }

    // Testing the old way of creating drivers with RenderProducts (before 7.2)
    AtNode *deep_driver_old = AiNodeLookUpByName(universe, AtString("/Render/Products/deep_old"));
    if (deep_driver_old == nullptr) {
        printf("[ERROR] '/Render/Products/deep_old' node is not found\n");
        errorCode++;
    } else {
        if (!AiNodeGetBool(deep_driver_old, AtString("append"))) {
            printf("[ERROR] 'append' is not set\n");
            errorCode++;
        }
        if (AiNodeGetFlt(deep_driver_old, AtString("alpha_tolerance"))!=10.f) {
            printf("[ERROR] 'alpha_tolerance' is not set. Expecting 10.f, got %f\n", AiNodeGetFlt(deep_driver_old, AtString("alpha_tolerance")));
            errorCode++;
        }
    }
    // Testing the new way of creating drivers with RenderProducts (before 7.2)
    AtNode *deep_driver_new = AiNodeLookUpByName(universe, AtString("/Render/Products/deep_new"));
    if (deep_driver_new == nullptr) {
        printf("[ERROR] '/Render/Products/deep_new' node is not found\n");
        errorCode++;
    } else {
        if (!AiNodeGetBool(deep_driver_new, AtString("append"))) {
            printf("[ERROR] 'append' is not set\n");
            errorCode++;
        }
        if (AiNodeGetFlt(deep_driver_new, AtString("alpha_tolerance"))!=10.f) {
            printf("[ERROR] 'alpha_tolerance' is not set. Expecting 10.f, got %f\n", AiNodeGetFlt(deep_driver_new, AtString("alpha_tolerance")));
            errorCode++;
        }
    }
    AiUniverseDestroy(universe);
    AiEnd(); // AI_SESSION_BATCH

    return errorCode;
}
