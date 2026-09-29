// Verifies the three HDARNOLD_GEOM_DEDUP modes of the render delegate by expanding the same
// USD scene once per mode and inspecting the resulting Arnold scene (no image comparison):
//
//   0 = None       : every mesh keeps its own geometry.
//   1 = Instances  : only point-instancer prototypes are deduplicated (the default).
//   2 = All        : plain non-instanced duplicates are deduplicated too (as ginstances).
//
// scene.usda contains:
//   - inst_a / inst_b : two point instancers with identical cube prototypes + same material
//                       (their prototypes deduplicate in modes 1 and 2);
//   - inst_c          : a point instancer whose cube differs by one vertex (never merged);
//   - plainA / plainB : two identical, non-instanced meshes (merged only in mode 2).
//
// We count polymeshes that actually carry geometry (a deduplicated duplicate keeps an empty,
// hidden shell node, so counting every 'polymesh' node would not distinguish the modes) and
// ginstance nodes (the non-instanced duplicates in "All" mode):
//
//   mode 0 : 5 geometry polymeshes, 0 ginstances   (inst_a, inst_b, inst_c, plainA, plainB)
//   mode 1 : 4 geometry polymeshes, 0 ginstances   (inst_a==inst_b, inst_c, plainA, plainB)
//   mode 2 : 3 geometry polymeshes, 1 ginstance    (inst_a==inst_b, inst_c, plainA==plainB)
//
// This test only links against Arnold (ai). It drives the deduplication - which lives in the
// render delegate, not the legacy translator - by expanding the usd procedural in its Hydra
// mode. It never loads the render delegate into its own process (that would clash with the
// static USD embedded in usd_proc), it goes through the procedural like a real render does.

#include <ai.h>

#include <cstdio>
#include <cstdlib>

namespace {

void SetEnv(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void UnsetEnv(const char* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// Number of polymesh nodes that actually hold geometry (a non-empty vertex list). A
// deduplicated duplicate leaves behind an empty, hidden polymesh shell whose instancer is
// redirected to the shared canonical; those must not be counted as a distinct geometry.
int GeometryPolymeshCount(AtUniverse* universe)
{
    int count = 0;
    AtNodeIterator* it = AiUniverseGetNodeIterator(universe, AI_NODE_SHAPE);
    while (!AiNodeIteratorFinished(it)) {
        AtNode* node = AiNodeIteratorGetNext(it);
        if (!AiNodeIs(node, AtString("polymesh")))
            continue;
        AtArray* vlist = AiNodeGetArray(node, AtString("vlist"));
        if (vlist != nullptr && AiArrayGetNumElements(vlist) > 0)
            ++count;
    }
    AiNodeIteratorDestroy(it);
    return count;
}

// Number of curves nodes that actually hold geometry (a non-empty point list). Mirrors
// GeometryPolymeshCount for the curves dedup path: a deduplicated duplicate leaves behind an
// empty, hidden curves shell whose instancer is redirected to the shared canonical.
int GeometryCurvesCount(AtUniverse* universe)
{
    int count = 0;
    AtNodeIterator* it = AiUniverseGetNodeIterator(universe, AI_NODE_SHAPE);
    while (!AiNodeIteratorFinished(it)) {
        AtNode* node = AiNodeIteratorGetNext(it);
        if (!AiNodeIs(node, AtString("curves")))
            continue;
        AtArray* points = AiNodeGetArray(node, AtString("points"));
        if (points != nullptr && AiArrayGetNumElements(points) > 0)
            ++count;
    }
    AiNodeIteratorDestroy(it);
    return count;
}

int NodeCount(AtUniverse* universe, const char* type)
{
    int count = 0;
    AtNodeIterator* it = AiUniverseGetNodeIterator(universe, AI_NODE_SHAPE);
    while (!AiNodeIteratorFinished(it)) {
        AtNode* node = AiNodeIteratorGetNext(it);
        if (AiNodeIs(node, AtString(type)))
            ++count;
    }
    AiNodeIteratorDestroy(it);
    return count;
}

struct ModeCase {
    const char* mode;
    const char* name;
    int geometryPolymeshes;
    int geometryCurves;
    int ginstances; // total ginstance nodes (mesh + curve duplicates in "All" mode)
};

} // namespace

int main(int /*argc*/, char** /*argv*/)
{
    AiBegin();
    AiMsgSetConsoleFlags(nullptr, AI_LOG_WARNINGS | AI_LOG_ERRORS);

    // Force the render-delegate (Hydra) path via the procedural's own "hydra" parameter,
    // regardless of the testsuite pass: remove PROCEDURAL_USE_HYDRA, which the 'usd' pass sets
    // to 0 and which would otherwise override the node parameter and select the legacy
    // translator (that does no deduplication).
    UnsetEnv("PROCEDURAL_USE_HYDRA");

    // Meshes and curves have parallel geometry groups, so each mode collapses them the same
    // way. "All" mode produces one mesh ginstance (plainB) and one curve ginstance
    // (curvePlainB), hence 2 total ginstances.
    const ModeCase cases[] = {
        {"0", "None", 5, 5, 0},
        {"1", "Instances", 4, 4, 0},
        {"2", "All", 3, 3, 2},
    };

    bool success = true;
    for (const ModeCase& c : cases) {
        // The render delegate reads HDARNOLD_GEOM_DEDUP fresh on construction, so setting it
        // here takes effect for this expansion even though earlier expansions ran with a
        // different value in the same process.
        SetEnv("HDARNOLD_GEOM_DEDUP", c.mode);

        AtUniverse* universe = AiUniverse();
        AtRenderSession* session = AiRenderSession(universe);
        AtNode* proc = AiNode(universe, AtString("usd"), AtString("dedup_proc"));
        AiNodeSetStr(proc, AtString("filename"), AtString("scene.usda"));
        AiNodeSetBool(proc, AtString("hydra"), true);
        AiProceduralExpand(proc, nullptr);

        const int geometryPolymeshes = GeometryPolymeshCount(universe);
        const int geometryCurves = GeometryCurvesCount(universe);
        const int ginstances = NodeCount(universe, "ginstance");
        const int instancers = NodeCount(universe, "instancer");

        printf(
            "[test_18101] mode %s (%s): geometry polymeshes=%d curves=%d ginstances=%d instancers=%d\n", c.mode,
            c.name, geometryPolymeshes, geometryCurves, ginstances, instancers);

        if (geometryPolymeshes != c.geometryPolymeshes || geometryCurves != c.geometryCurves ||
            ginstances != c.ginstances) {
            fprintf(
                stderr,
                "[test_18101] FAIL mode %s (%s): expected geometry polymeshes=%d curves=%d ginstances=%d, "
                "got geometry polymeshes=%d curves=%d ginstances=%d\n",
                c.mode, c.name, c.geometryPolymeshes, c.geometryCurves, c.ginstances, geometryPolymeshes,
                geometryCurves, ginstances);
            success = false;
        }

        AiRenderSessionDestroy(session);
        AiUniverseDestroy(universe);
    }

    AiEnd();
    return success ? 0 : 1;
}
