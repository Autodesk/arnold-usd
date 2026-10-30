// Implementation of the interactive USD tests framework, see interactive_usd_test.h
#include "interactive_usd_test.h"

#include <ai.h>

#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/plug/registry.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdUtils/stageCache.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#ifdef ENABLE_SCENE_INDEX
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <thread>

#ifdef INTERACTIVE_USD_EMBEDDED
// Defined by the node_loader of plugins/procedural/main.cpp, compiled in the same library
AI_EXPORT_LIB bool NodeLoader(int i, AtNodeLib* node);
#endif

// Set by the build, see libs/render_delegate/utils.cpp
#ifndef ARNOLD_ENABLE_HYDRA2
#define ARNOLD_ENABLE_HYDRA2 1
#endif

namespace interactive_usd_test {

namespace {

bool g_success = true;

bool Check_(bool condition, const std::string& message)
{
    if (!condition) {
        if (AiArnoldIsActive())
            AiMsgError("[interactive_usd_test] %s", message.c_str());
        else
            fprintf(stderr, "[interactive_usd_test] ERROR: %s\n", message.c_str());
        g_success = false;
    }
    return condition;
}

void SetEnv(const char* name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

// The same as HdArnoldIsSceneIndexEnabled (libs/render_delegate/utils.cpp), which isn't linked in the shared USD mode
bool SceneIndexEnabled()
{
    const char* value = std::getenv("USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX");
    if (value == nullptr)
        return ARNOLD_ENABLE_HYDRA2 != 0;
    std::string useSceneIndex(value);
    useSceneIndex.erase(std::remove(useSceneIndex.begin(), useSceneIndex.end(), ' '), useSceneIndex.end());
    return useSceneIndex != "0";
}

#ifdef INTERACTIVE_USD_EMBEDDED
// Must be called before AiBegin and before using USD.
// - ARNOLD_PLUGIN_PATH is cleared, so usd_proc and its private copy of USD don't get loaded from there. The usd_proc
//   shipped in the Arnold SDK plugins folder is still loaded by AiBegin, so its node entry is replaced.
// - The USD plugin resources installed next to the procedural are registered in the PlugRegistry. A few of them
//   (hdArnold, nodeRegistryArnold...) load usd_proc as their library, so these are copied to the test folder, with an
//   empty LibraryPath as their code is already linked in this program, like the USD plugins of a static build.
//   The plugin path environment variables are cleared, so no other arnold plugins are found. Note that the name of
//   this variable is a USD build setting, e.g. ARNOLD_PXR_PLUGINPATH_NAME in the USD builds made for Arnold.
bool SetupEnvironment()
{
    namespace fs = std::filesystem;
    SetEnv("ARNOLD_PLUGIN_PATH", "");
    SetEnv("PXR_PLUGINPATH_NAME", "");
    SetEnv("ARNOLD_PXR_PLUGINPATH_NAME", "");

    const char* proceduralPath = std::getenv("USD_PROCEDURAL_PATH");
    if (!Check_(proceduralPath != nullptr, "USD_PROCEDURAL_PATH is not set"))
        return false;
    const fs::path resources = fs::path(proceduralPath).parent_path() / "usd";
    if (!Check_(fs::exists(resources / "plugInfo.json"), "No USD plugin resources found in " + resources.string()))
        return false;

    const fs::path localResources = fs::current_path() / "usd_plugins";
    fs::remove_all(localResources);
    const std::regex procLibraryPath("\"LibraryPath\"\\s*:\\s*\"[^\"]*usd_proc[^\"]*\"");
    std::vector<std::string> pluginPaths;
    for (const auto& entry : fs::directory_iterator(resources)) {
        const fs::path plugInfo = entry.path() / "resources" / "plugInfo.json";
        if (!fs::exists(plugInfo))
            continue;
        std::ifstream in(plugInfo);
        std::stringstream contents;
        contents << in.rdbuf();
        const std::string json = contents.str();
        fs::path pluginPath = plugInfo.parent_path();
        if (std::regex_search(json, procLibraryPath)) {
            pluginPath = localResources / entry.path().filename() / "resources";
            fs::create_directories(pluginPath.parent_path());
            fs::copy(plugInfo.parent_path(), pluginPath, fs::copy_options::recursive);
            std::ofstream out(pluginPath / "plugInfo.json", std::ios::trunc);
            out << std::regex_replace(json, procLibraryPath, "\"LibraryPath\": \"\"");
        }
        pluginPaths.push_back(pluginPath.string());
    }
    return Check_(!PlugRegistry::GetInstance().RegisterPlugins(pluginPaths).empty(), "No USD plugins registered");
}
#else
// Must be called before AiBegin and before using USD.
// The test program, usd_proc and the arnold hydra plugins share the same USD. The procedural built here is loaded from
// ARNOLD_PLUGIN_PATH, set by the testsuite, and the arnold USD plugins are registered from the render delegate and
// schemas install folders, given by PREFIX_TEST_RENDERDELEGATE. They are registered in the PlugRegistry, rather than
// through the plugin path environment variable, since its name is a USD build setting (e.g.
// ARNOLD_PXR_PLUGINPATH_NAME in the USD builds made for Arnold).
bool SetupEnvironment()
{
    const char* prefixes = std::getenv("PREFIX_TEST_RENDERDELEGATE");
    if (!Check_(prefixes != nullptr, "PREFIX_TEST_RENDERDELEGATE is not set"))
        return false;
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::vector<std::string> pluginPaths;
    std::stringstream stream(prefixes);
    for (std::string path; std::getline(stream, path, separator);) {
        if (!path.empty())
            pluginPaths.push_back(path);
    }
    PlugRegistry::GetInstance().RegisterPlugins(pluginPaths);
    return Check_(PlugRegistry::GetInstance().GetPluginWithName("hdArnold") != nullptr,
        "The hdArnold USD plugin isn't registered, from " + std::string(prefixes));
}
#endif

Mode CurrentMode(Method method)
{
    const bool sceneIndex = SceneIndexEnabled();
    if (method == RenderDelegate)
        return sceneIndex ? DelegateHydra2 : DelegateHydra1;
#if defined(INTERACTIVE_USD_EMBEDDED) || defined(INTERACTIVE_USD_PROCEDURAL_HYDRA)
    // The procedural uses the hydra reader unless PROCEDURAL_USE_HYDRA is 0
    const char* useHydra = std::getenv("PROCEDURAL_USE_HYDRA");
    if (useHydra != nullptr && std::string(useHydra) == "0")
        return ProceduralLegacyReader;
    return sceneIndex ? ProceduralHydra2 : ProceduralHydra1;
#else
    // This procedural was built without the hydra reader (ENABLE_HYDRA_IN_USD_PROCEDURAL)
    return ProceduralLegacyReader;
#endif
}

const char* ModeName(Mode mode)
{
    switch (mode) {
        case ProceduralLegacyReader:
            return "procedural with the legacy reader";
        case ProceduralHydra1:
            return "procedural with the hydra 1 reader";
        case ProceduralHydra2:
            return "procedural with the hydra 2 reader";
        case DelegateHydra1:
            return "render delegate with hydra 1";
        case DelegateHydra2:
            return "render delegate with hydra 2";
        default:
            return "unknown mode";
    }
}

// Renders the stage, the scene being updated with the stage edits done since the previous render
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual bool Render(const std::string& filename, Image& image) = 0;
};

// The "usd" procedural compiled in this program, reading the stage through its cache_id
class ProceduralRenderer final : public Renderer {
public:
    ProceduralRenderer(const Scenario& scenario, const UsdStageRefPtr& stage) : _scenario(scenario), _stage(stage)
    {
        _cacheId = UsdUtilsStageCache::Get().Insert(stage);
        _universe = AiUniverse();
        _session = AiRenderSession(_universe, AI_SESSION_INTERACTIVE);

        AtNode* options = AiUniverseGetOptions(_universe);
        AiNodeSetInt(options, AtString("xres"), scenario.width);
        AiNodeSetInt(options, AtString("yres"), scenario.height);
        AiNodeSetInt(options, AtString("AA_samples"), scenario.aaSamples);
        AiNodeSetInt(options, AtString("GI_diffuse_depth"), 0);
        AiNodeSetInt(options, AtString("GI_specular_depth"), 0);

        _camera = AiNode(_universe, AtString("persp_camera"), AtString("interactive_usd_test_camera"));
        AiNodeSetPtr(options, AtString("camera"), _camera);

        AiNode(_universe, AtString("gaussian_filter"), AtString("interactive_usd_test_filter"));
        _driver = AiNode(_universe, AtString("driver_tiff"), AtString("interactive_usd_test_driver"));
        AiNodeSetStr(_driver, AtString("format"), AtString("float32"));
        AiNodeSetArray(options, AtString("outputs"),
            AiArray(1, 1, AI_TYPE_STRING, AtString("RGBA RGBA interactive_usd_test_filter interactive_usd_test_driver")));

        _proc = AiNode(_universe, AtString("usd"), AtString("interactive_usd_test_proc"));
        // The current USD stage cache ids fit in an int, see UsdArnoldReader::CreateNestedProc
        AiNodeSetInt(_proc, AtString("cache_id"), static_cast<int>(_cacheId.ToLongInt()));
    }

    ~ProceduralRenderer() override
    {
        AiRenderSessionDestroy(_session);
        AiUniverseDestroy(_universe);
        UsdUtilsStageCache::Get().Erase(_cacheId);
    }

    bool Render(const std::string& filename, Image& image) override
    {
        _UpdateCamera();
        // The stage edits don't modify the procedural node itself, so Arnold wouldn't call its procedural_update.
        // Toggling "interactive" (set back to false after each render) flags it as modified, as MtoA does (MTOA-2143).
        AiNodeSetBool(_proc, AtString("interactive"), true);
        AiNodeSetStr(_driver, AtString("filename"), AtString(filename.c_str()));
        const bool rendered = Check_(AiRender(_session, AI_RENDER_MODE_CAMERA) == AI_SUCCESS, "AiRender failed");
        AiNodeSetBool(_proc, AtString("interactive"), false);
        if (!rendered)
            return false;

        AtImage atImage;
        if (!Check_(AiReadImage(filename.c_str(), AI_TYPE_FLOAT, atImage), "Could not read " + filename))
            return false;
        image.width = atImage.width;
        image.height = atImage.height;
        image.rgba.assign(static_cast<size_t>(image.width) * image.height * 4, 0.f);
        const float* pixels = static_cast<const float*>(atImage.buffer);
        for (size_t i = 0; i < static_cast<size_t>(image.width) * image.height; ++i) {
            for (int c = 0; c < 4 && c < atImage.channels; ++c)
                image.rgba[i * 4 + c] = pixels[i * atImage.channels + c];
        }
        return true;
    }

private:
    // The arnold camera matches the stage camera, the arnold horizontal field of view being used with the resolution
    void _UpdateCamera()
    {
        const UsdGeomCamera camera(_stage->GetPrimAtPath(_scenario.camera));
        if (!Check_(bool(camera), "No camera at " + _scenario.camera.GetString()))
            return;
        const GfCamera gfCamera = camera.GetCamera(UsdTimeCode::Default());
        const GfMatrix4d& transform = gfCamera.GetTransform();
        AtMatrix matrix;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j)
                matrix[i][j] = static_cast<float>(transform[i][j]);
        }
        AiNodeSetMatrix(_camera, AtString("matrix"), matrix);
        AiNodeSetFlt(_camera, AtString("fov"), gfCamera.GetFieldOfView(GfCamera::FOVHorizontal));
    }

    const Scenario& _scenario;
    UsdStageRefPtr _stage;
    UsdStageCache::Id _cacheId;
    AtUniverse* _universe = nullptr;
    AtRenderSession* _session = nullptr;
    AtNode* _camera = nullptr;
    AtNode* _driver = nullptr;
    AtNode* _proc = nullptr;
};

// Syncs and executes a render pass, like the render task of HdxTaskController
class RenderTask final : public HdTask {
public:
    RenderTask(const HdRenderPassSharedPtr& renderPass, const HdRenderPassStateSharedPtr& renderPassState,
        const SdfPath& cameraPath)
        : HdTask(SdfPath::EmptyPath()),
          _renderPass(renderPass),
          _renderPassState(renderPassState),
          _cameraPath(cameraPath),
          _renderTags({HdRenderTagTokens->geometry})
    {
    }

    void Sync(HdSceneDelegate*, HdTaskContext*, HdDirtyBits* dirtyBits) override
    {
        _renderPass->Sync();
        *dirtyBits = HdChangeTracker::Clean;
    }

    void Prepare(HdTaskContext*, HdRenderIndex* renderIndex) override
    {
        _renderPassState->SetCamera(
            static_cast<const HdCamera*>(renderIndex->GetSprim(HdPrimTypeTokens->camera, _cameraPath)));
        _renderPassState->Prepare(renderIndex->GetResourceRegistry());
    }

    void Execute(HdTaskContext*) override { _renderPass->Execute(_renderPassState, _renderTags); }

    const TfTokenVector& GetRenderTags() const override { return _renderTags; }

private:
    HdRenderPassSharedPtr _renderPass;
    HdRenderPassStateSharedPtr _renderPassState;
    SdfPath _cameraPath;
    TfTokenVector _renderTags;
};

// The arnold render delegate, fed by a UsdImagingDelegate (Hydra 1) or the UsdImaging scene indices (Hydra 2)
class DelegateRenderer final : public Renderer {
public:
    DelegateRenderer(const Scenario& scenario, const UsdStageRefPtr& stage) : _scenario(scenario)
    {
        // Created through the plugin registry, as hosts do: this also gives the delegate its renderer display
        // name, which the render index needs to apply the arnold scene index plugins.
        _renderDelegate =
            HdRendererPluginRegistry::GetInstance().CreateRenderDelegate(TfToken("HdArnoldRendererPlugin"));
        if (!Check_(bool(_renderDelegate), "Could not create the HdArnoldRendererPlugin render delegate"))
            return;
        // The render delegate sets its own log flags
        AiMsgSetConsoleFlags(nullptr, AI_LOG_INFO | AI_LOG_WARNINGS | AI_LOG_ERRORS);
        _renderDelegate->SetRenderSetting(TfToken("arnold:global:AA_samples"), VtValue(scenario.aaSamples));
        _renderIndex = HdRenderIndex::New(_renderDelegate.Get(), HdDriverVector());

#ifdef ENABLE_SCENE_INDEX
        if (SceneIndexEnabled()) {
            UsdImagingCreateSceneIndicesInfo info;
            info.stage = stage;
            const UsdImagingSceneIndices sceneIndices = UsdImagingCreateSceneIndices(info);
            _stageSceneIndex = sceneIndices.stageSceneIndex;
            _sceneIndex = sceneIndices.finalSceneIndex;
            _renderIndex->InsertSceneIndex(_sceneIndex, SdfPath::AbsoluteRootPath());
        } else
#endif
        {
            _imagingDelegate = std::make_unique<UsdImagingDelegate>(_renderIndex, SdfPath::AbsoluteRootPath());
            _imagingDelegate->Populate(stage->GetPseudoRoot());
        }

        const SdfPath bufferPath("/interactive_usd_test/color");
        _colorBuffer =
            static_cast<HdRenderBuffer*>(_renderDelegate->CreateBprim(HdPrimTypeTokens->renderBuffer, bufferPath));
        _colorBuffer->Allocate(GfVec3i(scenario.width, scenario.height, 1), HdFormatFloat32Vec4, false);

        HdRenderPassAovBinding binding;
        binding.aovName = HdAovTokens->color;
        binding.renderBuffer = _colorBuffer;
        binding.renderBufferId = bufferPath;
        binding.clearValue = VtValue(GfVec4f(0.f));

        _renderPass = _renderDelegate->CreateRenderPass(
            _renderIndex, HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->hull)));
        HdRenderPassStateSharedPtr renderPassState = _renderDelegate->CreateRenderPassState();
        renderPassState->SetAovBindings({binding});
        renderPassState->SetFraming(
            CameraUtilFraming(GfRect2i(GfVec2i(0, 0), scenario.width, scenario.height)));
        _tasks = {std::make_shared<RenderTask>(_renderPass, renderPassState, scenario.camera)};
    }

    ~DelegateRenderer() override
    {
        // Tear down in the reverse order: tasks and render pass, scene delegates, render index, render delegate
        _tasks.clear();
        _renderPass.reset();
        if (_colorBuffer)
            _renderDelegate->DestroyBprim(_colorBuffer);
        _imagingDelegate.reset();
#ifdef ENABLE_SCENE_INDEX
        if (_sceneIndex)
            _renderIndex->RemoveSceneIndex(_sceneIndex);
#endif
        delete _renderIndex;
    }

    bool Render(const std::string& filename, Image& image) override
    {
        if (!_renderIndex)
            return false;
#ifdef ENABLE_SCENE_INDEX
        if (_stageSceneIndex)
            _stageSceneIndex->ApplyPendingUpdates();
#endif
        if (_imagingDelegate)
            _imagingDelegate->ApplyPendingUpdates();

        // Executes the tasks until the render pass reports the render as converged
        const auto start = std::chrono::steady_clock::now();
        while (true) {
            _engine.Execute(_renderIndex, &_tasks);
            if (_renderPass->IsConverged())
                break;
            if (!Check_(std::chrono::steady_clock::now() - start < std::chrono::seconds(60),
                    "The render did not converge"))
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Hydra render buffers are stored bottom-up
        image.width = _scenario.width;
        image.height = _scenario.height;
        const size_t rowSize = static_cast<size_t>(image.width) * 4;
        image.rgba.resize(rowSize * image.height);
        const float* data = static_cast<const float*>(_colorBuffer->Map());
        for (int y = 0; y < image.height; ++y)
            memcpy(&image.rgba[y * rowSize], data + (image.height - 1 - y) * rowSize, rowSize * sizeof(float));
        _colorBuffer->Unmap();

        AtImage atImage;
        atImage.width = atImage.full_width = image.width;
        atImage.height = atImage.full_height = image.height;
        atImage.channels = 4;
        atImage.format = AI_TYPE_FLOAT;
        atImage.buffer = AiMalloc(image.rgba.size() * sizeof(float));
        memcpy(atImage.buffer, image.rgba.data(), image.rgba.size() * sizeof(float));
        Check_(AiWriteImage(filename.c_str(), atImage), "Could not write " + filename);
        return true;
    }

private:
    const Scenario& _scenario;
    HdPluginRenderDelegateUniqueHandle _renderDelegate;
    HdRenderIndex* _renderIndex = nullptr;
    std::unique_ptr<UsdImagingDelegate> _imagingDelegate;
#ifdef ENABLE_SCENE_INDEX
    UsdImagingStageSceneIndexRefPtr _stageSceneIndex;
    HdSceneIndexBaseRefPtr _sceneIndex;
#endif
    HdRenderBuffer* _colorBuffer = nullptr;
    HdRenderPassSharedPtr _renderPass;
    HdEngine _engine;
    HdTaskSharedPtrVector _tasks;
};

#ifdef INTERACTIVE_USD_EMBEDDED
// Installs the node entry of the procedural compiled in this program, replacing the one of the SDK's usd_proc
bool InstallProcedural()
{
    AtNodeLib lib;
    if (!Check_(NodeLoader(0, &lib), "NodeLoader failed"))
        return false;
    if (AiNodeEntryLookUp(AtString(lib.name)))
        AiNodeEntryUninstall(lib.name);
    AiNodeEntryInstall(lib.node_type, lib.output_type, lib.name, "interactive_usd_test",
        static_cast<const AtNodeMethods*>(lib.methods), lib.version);
    const AtNodeEntry* entry = AiNodeEntryLookUp(AtString(lib.name));
    return Check_(entry != nullptr && AiNodeEntryGetDerivedType(entry) == AI_NODE_SHAPE_PROCEDURAL,
        "Could not install the usd procedural");
}
#else
// Makes sure the "usd" node entry comes from the procedural built here (USD_PROCEDURAL_PATH), rather than from the
// usd_proc shipped in the SDK plugins folder, which AiBegin loads too and which has its own static USD
bool InstallProcedural()
{
    namespace fs = std::filesystem;
    const char* proceduralPath = std::getenv("USD_PROCEDURAL_PATH");
    if (!Check_(proceduralPath != nullptr, "USD_PROCEDURAL_PATH is not set"))
        return false;
    const AtString name("usd");
    auto isBuiltProcedural = [&]() {
        const AtNodeEntry* entry = AiNodeEntryLookUp(name);
        std::error_code error;
        return entry != nullptr && fs::equivalent(AiNodeEntryGetFilename(entry), proceduralPath, error);
    };
    if (!isBuiltProcedural()) {
        if (AiNodeEntryLookUp(name))
            AiNodeEntryUninstall(name.c_str());
        AiLoadPlugins(proceduralPath);
    }
    return Check_(isBuiltProcedural(), std::string("Could not load the usd procedural ") + proceduralPath);
}
#endif

void RunMethod(const Scenario& scenario, Method method, bool writeTestRender)
{
    const Mode mode = CurrentMode(method);
    const char* modeName = ModeName(mode);
    const char* methodName = method == Procedural ? "procedural" : "delegate";
    AiMsgInfo("[interactive_usd_test] running the scenario with the %s", modeName);

    if (method == Procedural && !InstallProcedural())
        return;
    UsdStageRefPtr stage = scenario.createStage();
    if (!Check_(bool(stage), "Could not create the stage"))
        return;
    std::unique_ptr<Renderer> renderer;
    if (method == Procedural)
        renderer = std::make_unique<ProceduralRenderer>(scenario, stage);
    else
        renderer = std::make_unique<DelegateRenderer>(scenario, stage);

    const size_t referenceStep = scenario.referenceStep < 0 ? scenario.steps.size() - 1
                                                            : static_cast<size_t>(scenario.referenceStep);
    for (size_t i = 0; i < scenario.steps.size(); ++i) {
        const Step& step = scenario.steps[i];
        if (step.edit)
            step.edit(stage);
        const std::string filename = writeTestRender && i == referenceStep
            ? std::string("testrender.tif")
            : std::string(methodName) + "_step" + std::to_string(i) + ".tif";
        AiMsgInfo("[interactive_usd_test] %s, step %zu: %s", modeName, i, step.description.c_str());
        Image image;
        if (!renderer->Render(filename, image))
            return;
        const std::string error = step.check ? step.check(image) : std::string();

        const auto knownIssue = std::find_if(step.knownIssues.begin(), step.knownIssues.end(),
            [mode](const KnownIssue& issue) { return (issue.modes & mode) != 0; });
        const std::string prefix =
            std::string(modeName) + ", step " + std::to_string(i) + " (" + step.description + ")";
        if (knownIssue == step.knownIssues.end())
            Check_(error.empty(), prefix + ": " + error);
        else if (!error.empty())
            AiMsgWarning("[interactive_usd_test] %s: %s, known issue: %s", prefix.c_str(), error.c_str(),
                knownIssue->description.c_str());
        else
            AiMsgWarning("[interactive_usd_test] %s now passes, known issue \"%s\" might be fixed", prefix.c_str(),
                knownIssue->description.c_str());
    }
}

std::string FormatColor(const GfVec4f& c)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "(%.4f, %.4f, %.4f, %.4f)", c[0], c[1], c[2], c[3]);
    return buffer;
}

} // namespace

GfVec4f Image::Average(float u, float v, int radius) const
{
    const int cx = std::clamp(static_cast<int>(u * width), 0, width - 1);
    const int cy = std::clamp(static_cast<int>(v * height), 0, height - 1);
    GfVec4f sum(0.f);
    int count = 0;
    for (int y = std::max(cy - radius, 0); y <= std::min(cy + radius, height - 1); ++y) {
        for (int x = std::max(cx - radius, 0); x <= std::min(cx + radius, width - 1); ++x) {
            const float* pixel = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            sum += GfVec4f(pixel[0], pixel[1], pixel[2], pixel[3]);
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<float>(count) : sum;
}

Check ExpectColor(Color color, float u, float v)
{
    return [color, u, v](const Image& image) -> std::string {
        const GfVec4f c = image.Average(u, v);
        const float maxRgb = std::max({c[0], c[1], c[2]});
        const float minRgb = std::min({c[0], c[1], c[2]});
        bool ok = false;
        const char* name = "";
        switch (color) {
            case Color::Red:
            case Color::Green:
            case Color::Blue: {
                const int dominant = color == Color::Red ? 0 : (color == Color::Green ? 1 : 2);
                name = color == Color::Red ? "red" : (color == Color::Green ? "green" : "blue");
                ok = c[3] > 0.9f && c[dominant] > 0.05f;
                for (int i = 0; i < 3; ++i) {
                    if (i != dominant)
                        ok = ok && c[dominant] > 3.f * c[i];
                }
                break;
            }
            case Color::Grey:
                name = "grey";
                ok = c[3] > 0.9f && minRgb > 0.05f && maxRgb < 1.5f * minRgb;
                break;
            case Color::Black:
                name = "black";
                ok = c[3] > 0.9f && maxRgb < 0.02f;
                break;
            case Color::Empty:
                name = "empty";
                ok = c[3] < 0.1f;
                break;
        }
        if (ok)
            return std::string();
        char position[64];
        snprintf(position, sizeof(position), "(%.2f, %.2f)", u, v);
        return std::string("expected ") + name + " at " + position + ", got " + FormatColor(c);
    };
}

Check ExpectColor(Color color, const GfVec2f& position) { return ExpectColor(color, position[0], position[1]); }

Check AllOf(const std::vector<Check>& checks)
{
    return [checks](const Image& image) -> std::string {
        std::string errors;
        for (const Check& check : checks) {
            const std::string error = check(image);
            if (!error.empty())
                errors += (errors.empty() ? "" : "; ") + error;
        }
        return errors;
    };
}

int Run(const Scenario& scenario)
{
    if (!SetupEnvironment())
        return 1;
    AiBegin(AI_SESSION_INTERACTIVE);
    AiMsgSetConsoleFlags(nullptr, AI_LOG_INFO | AI_LOG_WARNINGS | AI_LOG_ERRORS);

    bool first = true;
    for (Method method : {Procedural, RenderDelegate}) {
        if (scenario.methods & method) {
            RunMethod(scenario, method, first);
            first = false;
        }
    }

    AiEnd();
    return g_success ? 0 : 1;
}

void DefineCamera(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3d& position)
{
    UsdGeomCamera camera = UsdGeomCamera::Define(stage, path);
    UsdGeomXformCommonAPI(camera.GetPrim()).SetTranslate(position);
    camera.CreateFocalLengthAttr().Set(35.f);
    camera.CreateHorizontalApertureAttr().Set(36.f);
    camera.CreateVerticalApertureAttr().Set(27.f);
}

GfVec2f ScreenPosition(const GfVec3d& point, const GfVec3d& cameraPosition)
{
    // Half the horizontal aperture over the focal length set by DefineCamera, and the 160x120 aspect ratio
    const double tanHalfFov = 18.0 / 35.0;
    const double distance = cameraPosition[2] - point[2];
    const double halfWidth = distance * tanHalfFov;
    const double halfHeight = halfWidth * 120.0 / 160.0;
    return GfVec2f(static_cast<float>(0.5 + 0.5 * (point[0] - cameraPosition[0]) / halfWidth),
        static_cast<float>(0.5 - 0.5 * (point[1] - cameraPosition[1]) / halfHeight));
}

void DefineDistantLight(const UsdStageRefPtr& stage, const SdfPath& path, float intensity)
{
    UsdLuxDistantLight light = UsdLuxDistantLight::Define(stage, path);
    // Tilted, so that its specular highlight isn't in the center of the surfaces facing the camera
    UsdGeomXformCommonAPI(light.GetPrim()).SetRotate(GfVec3f(40.f, 40.f, 0.f));
    light.CreateIntensityAttr().Set(intensity);
    // Without normalize, the intensity of a distant light is spread over its (0.53 degrees) angle
    light.CreateNormalizeAttr().Set(true);
}

UsdShadeMaterial DefinePreviewMaterial(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3f& diffuseColor)
{
    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, path);
    UsdShadeShader shader = UsdShadeShader::Define(stage, path.AppendChild(TfToken("surface")));
    shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f).Set(diffuseColor);
    shader.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(0.5f);
    material.CreateSurfaceOutput().ConnectToSource(shader.ConnectableAPI(), TfToken("surface"));
    return material;
}

void BindMaterial(const UsdStageRefPtr& stage, const SdfPath& primPath, const UsdShadeMaterial& material)
{
    UsdShadeMaterialBindingAPI::Apply(stage->GetPrimAtPath(primPath)).Bind(material);
}

void SetTranslate(const UsdStageRefPtr& stage, const SdfPath& path, const GfVec3d& translate)
{
    UsdGeomXformCommonAPI(stage->GetPrimAtPath(path)).SetTranslate(translate);
}

} // namespace interactive_usd_test
