// Regression test for #2719: HdArnoldRenderDelegate's pause/resume state
// machine (_paused, driving IsPaused()) could get stuck reporting "paused"
// while the render was actually active again, because a scene edit (any
// Interrupt() call, e.g. from a camera/mesh/light Sync()) cancels an
// in-flight pause without clearing the bookkeeping. Stop() had the same gap.
//
// This talks to HdArnoldRenderDelegate/HdArnoldRenderParam directly rather
// than through a UsdStage + Hydra render index: the state machine under test
// lives entirely in those two classes, and HdRenderDelegate::IsPaused() isn't
// exposed through UsdImagingGLEngine or Python bindings, so there is no way
// to observe it from a normal scene.usda-driven test.
//
// The scene is built with plain AiNode() calls rather than AiSceneLoad() of a
// .usda file: this test statically links USD (via render_delegate) directly
// into this executable, and AiBegin() auto-loads every plugin under
// ARNOLD_PLUGIN_PATH -- including usd_proc.dylib, which embeds its own private
// static copy of USD. Having two independent copies of USD's global registries
// (TfEnvSetting, TfType, ...) in one process crashes ("Multiple definitions of
// TfEnvSetting variable detected", then SIGSEGV). Since this test needs no USD
// scene reading at all, ARNOLD_PLUGIN_PATH is cleared below so no plugin
// (usd_proc.dylib or otherwise) gets loaded in the first place.
#include <ai.h>

#include <render_delegate.h>
#include <render_param.h>

#include <pxr/base/tf/token.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool g_success = true;

bool Check(bool condition, const char* message)
{
    if (!condition) {
        AiMsgError("[test_2719] %s", message);
        g_success = false;
    }
    return condition;
}

// Waits (bounded) for the render session to reach a specific Arnold render status,
// pumping UpdateRender() every iteration exactly like a real render pass would on
// every Sync()/Execute() tick. This matters after Interrupt(): the actual
// AiRenderRestart() call that resumes the render lives inside UpdateRender()'s
// PAUSED-case, not in Arnold's own background threads, so nothing advances the
// render session unless something keeps calling UpdateRender().
bool WaitForStatus(HdArnoldRenderParam* renderParam, AtRenderSession* session, AtRenderStatus status, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    while (AiRenderGetStatus(session) != status) {
        renderParam->UpdateRender();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed.count() > timeoutMs) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// Drives UpdateRender() until it reports Converged/Aborted or the timeout elapses.
HdArnoldRenderParam::Status RunToCompletion(HdArnoldRenderParam* renderParam, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    HdArnoldRenderParam::Status status = HdArnoldRenderParam::Status::Converging;
    while (status == HdArnoldRenderParam::Status::Converging) {
        status = renderParam->UpdateRender();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed.count() > timeoutMs) {
            break;
        }
    }
    return status;
}

// Builds a small, deliberately expensive scene directly with AiNode() calls (see the
// top-of-file comment for why this doesn't load a .usda file instead). The high
// AA_samples/resolution keep the render busy long enough for the Pause()/Interrupt()
// sequence in main() to reliably land while AiRenderGetStatus() still reports
// RENDERING. Do not lower these to "optimize" the test -- that would make it flaky.
void BuildScene(AtUniverse* universe)
{
    AtNode* options = AiUniverseGetOptions(universe);
    AiNodeSetInt(options, AtString("xres"), 320);
    AiNodeSetInt(options, AtString("yres"), 240);
    AiNodeSetInt(options, AtString("AA_samples"), 32);
    AiNodeSetInt(options, AtString("GI_diffuse_depth"), 2);

    AtNode* camera = AiNode(universe, AtString("persp_camera"), AtString("test_2719_camera"));
    AiNodeSetVec(camera, AtString("position"), 0.f, 0.f, 6.f);
    AiNodeSetVec(camera, AtString("look_at"), 0.f, 0.f, 0.f);
    AiNodeSetPtr(options, AtString("camera"), camera);

    AtNode* light = AiNode(universe, AtString("distant_light"), AtString("test_2719_light"));
    AiNodeSetFlt(light, AtString("intensity"), 1.5f);

    AtNode* shader = AiNode(universe, AtString("standard_surface"), AtString("test_2719_shader"));
    AiNodeSetRGB(shader, AtString("base_color"), 0.6f, 0.1f, 0.1f);
    AiNodeSetFlt(shader, AtString("specular_roughness"), 0.4f);

    AtNode* sphere = AiNode(universe, AtString("sphere"), AtString("test_2719_sphere"));
    AiNodeSetFlt(sphere, AtString("radius"), 1.f);
    AiNodeSetPtr(sphere, AtString("shader"), shader);

    AiNode(universe, AtString("gaussian_filter"), AtString("test_2719_filter"));
    AtNode* driver = AiNode(universe, AtString("driver_tiff"), AtString("test_2719_driver"));
    AiNodeSetStr(driver, AtString("filename"), AtString("testrender.tif"));

    AtArray* outputs = AiArray(1, 1, AI_TYPE_STRING, AtString("RGBA RGBA test_2719_filter test_2719_driver"));
    AiNodeSetArray(options, AtString("outputs"), outputs);
}

} // namespace

int main(int, char**)
{
    // See the top-of-file comment: this test needs no Arnold plugins (no USD scene
    // reading, no custom shaders), and loading one (usd_proc.dylib in particular)
    // crashes since it embeds its own private static copy of USD alongside the one
    // statically linked directly into this executable.
    unsetenv("ARNOLD_PLUGIN_PATH");

    AiBegin(AI_SESSION_INTERACTIVE);
    AiMsgSetConsoleFlags(nullptr, AI_LOG_ALL);

    {
        HdArnoldRenderDelegate delegate(false, TfToken());
        BuildScene(delegate.GetUniverse());

        auto* renderParam = static_cast<HdArnoldRenderParam*>(delegate.GetRenderParam());
        AtRenderSession* session = delegate.GetRenderSession();

        // Kick off the render (NOT_STARTED -> RENDERING) and wait for it to actually
        // be in flight: Pause()/Interrupt() below need to observe a live
        // AI_RENDER_STATUS_RENDERING session for this test to be meaningful. If the
        // scene renders too fast for this to ever be observed, it needs to be made
        // heavier (more samples/resolution), not left silently un-tested.
        renderParam->UpdateRender();
        if (Check(WaitForStatus(renderParam, session, AI_RENDER_STATUS_RENDERING, 5000),
                "Render never reached RENDERING status -- BuildScene() may need to be heavier/slower "
                "for this test to be meaningful")) {

            // --- A scene edit while paused must cancel the pause, not leave it stuck ---
            // Arnold's AiRenderPause() gate (Arnold 7.5.4+) has no mechanism to discard
            // accumulated samples across a scene edit, so any edit forces a real
            // interrupt+restart, which must be reflected in IsPaused() rather than left
            // claiming "paused" while the render is actually active again.
            delegate.Pause();
            Check(delegate.IsPaused(), "IsPaused() should be true right after Pause()");

            renderParam->Interrupt(); // stand-in for a camera/mesh/light Sync() edit
            Check(!delegate.IsPaused(),
                "IsPaused() stayed true after a scene-edit Interrupt() cancelled the pause (#2719)");

            Check(WaitForStatus(renderParam, session, AI_RENDER_STATUS_RENDERING, 5000),
                "Render did not resume after the edit-cancelled pause");

            // --- Resume() must clear the flag when there was no edit in between ---
            delegate.Pause();
            Check(delegate.IsPaused(), "IsPaused() should be true right after a second Pause()");
            delegate.Resume();
            Check(!delegate.IsPaused(), "IsPaused() should be false right after Resume()");

            // --- Stop() must clear the flag too, matching Restart()'s existing contract ---
            delegate.Pause();
            Check(delegate.IsPaused(), "IsPaused() should be true right after a third Pause()");
#if PXR_VERSION >= 2203
            delegate.Stop(true);
#else
            delegate.Stop();
#endif
            Check(!delegate.IsPaused(), "IsPaused() stayed true after Stop() (#2719)");
        }

        // Let the render actually finish so the delegate tears down cleanly.
        delegate.Restart();
        const auto finalStatus = RunToCompletion(renderParam, 30000);
        Check(finalStatus == HdArnoldRenderParam::Status::Converged, "Render did not converge by the end of the test");
    }

    AiEnd();
    return g_success ? 0 : 1;
}
