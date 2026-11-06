// Framework for interactive USD tests: a test describes a scenario, i.e. a UsdStage and a list of edits, each
// followed by a render whose image is checked. The scenario runs with each requested method:
//  - Procedural: a "usd" procedural node pointing at the stage cache_id, re-rendered in an interactive render
//    session after each edit, as e.g. MtoA does with its usd proxy shapes
//  - RenderDelegate: the HdArnoldRendererPlugin render delegate driven like a Hydra host (usdview, Houdini...): a
//    render index fed by UsdImaging, a render pass writing into a render buffer, executed by an HdEngine
// The testsuite pass selects the procedural reader (PROCEDURAL_USE_HYDRA) and Hydra 1 or 2
// (USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX), see Mode.
//
// A test using it only needs a test.cpp whose INTERACTIVE_USD_TEST_MAIN() calls Run(), and
// PARAMS: {'program_env': 'interactive_usd'} in its README, see TEST_PROGRAM_ENVS in testsuite/SConscript. When a single
// test runs, its test.cpp is linked in its own program. When several run, their test.cpp are linked in a single
// program shared by them, which runs the test named by its first argument (see TEST_PROGRAM_COMBINED_ENVS in
// testsuite/SConscript): this avoids linking the static USD in every test program. As a consequence:
//  - the entry point must be INTERACTIVE_USD_TEST_MAIN(), not main(), and the README must not set a 'script'
//  - helpers and globals must be in an anonymous namespace, not to clash with those of the other tests
//  - globals are constructed in the process of every test, before Run() sets up the environment: only use trivial
//    values (SdfPath, TfToken, GfVec3f...), nothing using USD plugins or Arnold, and build stages in createStage
// The test program, the procedural and the render delegate must share the same USD, for the stage created by the test
// to be visible to them:
//  - with a static USD, usd_proc embeds its own private copy of USD, so the usd procedural, the arnold hydra plugins
//    and USD are statically linked in the test program instead
//  - with a monolithic or shared USD, the test program loads the usd_proc and hdArnold built in this repository
//
// Every render is saved as <method>_step<N>.tif in the test folder, except the render of the reference step (the last
// one by default) with the first method, saved as testrender.tif to be compared against the reference image.
#pragma once

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/material.h>

#include <functional>
#include <string>
#include <vector>

// The entry point of a test. In a program shared by several tests, TEST_PROGRAM_ENTRY is defined when compiling each
// test.cpp, to a function name unique to the test that the shared program's main() calls (see tools/test/testsuite.py)
#ifdef TEST_PROGRAM_ENTRY
#define INTERACTIVE_USD_TEST_MAIN() int TEST_PROGRAM_ENTRY(int, char**)
#else
#define INTERACTIVE_USD_TEST_MAIN() int main(int, char**)
#endif

namespace interactive_usd_test {

PXR_NAMESPACE_USING_DIRECTIVE

enum Method : unsigned {
    Procedural = 1 << 0,
    RenderDelegate = 1 << 1,
    AllMethods = Procedural | RenderDelegate,
};

// The configurations a scenario runs in, depending on the method and the testsuite pass
enum Mode : unsigned {
    ProceduralLegacyReader = 1 << 0, // "usd" pass
    ProceduralHydra1 = 1 << 1,       // "hydra" pass
    ProceduralHydra2 = 1 << 2,       // "hydra2" pass
    DelegateHydra1 = 1 << 3,         // "usd" and "hydra" passes
    DelegateHydra2 = 1 << 4,         // "hydra2" pass
    ProceduralHydra = ProceduralHydra1 | ProceduralHydra2,
    AnyProcedural = ProceduralLegacyReader | ProceduralHydra,
    AnyDelegate = DelegateHydra1 | DelegateHydra2,
    AnyMode = AnyProcedural | AnyDelegate,
};

// A rendered RGBA image, stored top-down. Positions are normalized, (0, 0) being the top left corner.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<float> rgba;

    // Average color of the pixels around a position
    GfVec4f Average(float u, float v, int radius = 2) const;
};

// Checks a rendered image, returning a description of the failure, or an empty string if it is as expected
using Check = std::function<std::string(const Image&)>;

// What a pixel is expected to look like. Red, Green, Blue and Grey are lit opaque surfaces, with that dominant color
// or a neutral one, Black is an opaque unlit surface and Empty is the background.
enum class Color { Red, Green, Blue, Grey, Black, Empty };

Check ExpectColor(Color color, float u = 0.5f, float v = 0.5f);
Check ExpectColor(Color color, const GfVec2f& position);
Check AllOf(const std::vector<Check>& checks);

// A check that is known to fail in some modes. It is then reported as a warning instead of failing the test, and a
// warning also tells when it passes, so the known issue can be removed once fixed. When the reference step is affected,
// the per-pass reference image of these modes (e.g. ref/reference.usd.tif) is rendered with the issue: fixing it also
// makes the image comparison fail, until that reference image is regenerated or removed.
struct KnownIssue {
    unsigned modes;
    std::string description;
};

enum Platform : unsigned {
    Windows = 1 << 0,
    Linux = 1 << 1,
    Darwin = 1 << 2,
    AllPlatforms = Windows | Linux | Darwin,
};

// Modes in which a scenario isn't run on some platforms, e.g. because it crashes there. The reason is logged as a
// warning. On every platform, these modes don't write the render compared against the reference image, the next
// method does, so that the comparison is the same everywhere (see the per-pass reference images, e.g.
// ref/reference.hydra.tif).
struct SkippedModes {
    unsigned modes;
    std::string reason;
    unsigned platforms = AllPlatforms;
};

struct Step {
    std::string description;
    // Edits the stage before the render, nothing is edited if empty (e.g. for the initial render)
    std::function<void(const UsdStageRefPtr&)> edit;
    Check check;
    std::vector<KnownIssue> knownIssues;
};

struct Scenario {
    // Creates the stage, called once per method as the steps edit it
    std::function<UsdStageRefPtr()> createStage;
    std::vector<Step> steps;
    unsigned methods = AllMethods;
    // The camera to render the stage from. The procedural uses an arnold camera matching it, updated at every step.
    SdfPath camera = SdfPath("/World/camera");
    int width = 160;
    int height = 120;
    int aaSamples = 3;
    // The usd_override_double_sided option, true by default since Arnold 7.5, renders every mesh double-sided whatever its
    // doubleSided attribute. Set it to false to check the sidedness and orientation of meshes.
    bool usdOverrideDoubleSided = true;
    // The step whose render is compared against the reference image, the last one if negative
    int referenceStep = -1;
    std::vector<SkippedModes> skippedModes;
};

// Runs the scenario with all its methods, returning the program exit code
int Run(const Scenario& scenario);

// Helpers to build and edit stages

// A camera at the given position, looking down -Z, with a 54.43 degrees horizontal field of view
void DefineCamera(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3d& position);
// Normalized image position of a world position, as seen by a camera made by DefineCamera at cameraPosition, with
// the default 160x120 resolution
GfVec2f ScreenPosition(const GfVec3d& point, const GfVec3d& cameraPosition = GfVec3d(0, 0, 6));
// A normalized distant light, pointing down -Z and tilted by 40 degrees around X and Y
void DefineDistantLight(const UsdStageRefPtr& stage, const SdfPath& path, float intensity = 3.f);
// A UsdPreviewSurface material, whose shader is <path>/surface
UsdShadeMaterial DefinePreviewMaterial(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3f& diffuseColor);
void BindMaterial(const UsdStageRefPtr& stage, const SdfPath& primPath, const UsdShadeMaterial& material);
void SetTranslate(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3d& translate);

} // namespace interactive_usd_test
