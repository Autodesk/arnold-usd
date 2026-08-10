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

// Drives UpdateRender() until it reports Converged/Aborted or the timeout elapses. Sleeps between ticks like
// WaitForStatus() does: a tight spin loop here would steal a core from the render itself, which on a low-core
// machine is the difference between converging inside the timeout and not.
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
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return status;
}

// IsPaused()/IsStopped() are only part of HdRenderDelegate from USD 22.03 on; query the override where it exists
// (that is the API hosts actually call) and fall back to the render param that backs it otherwise.
bool IsPaused(const HdArnoldRenderDelegate& delegate, HdArnoldRenderParam* renderParam)
{
#if PXR_VERSION >= 2203
    return delegate.IsPaused();
#else
    return renderParam->IsPaused();
#endif
}

bool IsStopped(const HdArnoldRenderDelegate& delegate, HdArnoldRenderParam* renderParam)
{
#if PXR_VERSION >= 2203
    return delegate.IsStopped();
#else
    return renderParam->IsStopped();
#endif
}

// True when Arnold reports the render threads as parked at the AiRenderPause() gate. Unlike IsPaused() above this
// is Arnold's own view of it, so it catches a pause that our bookkeeping claims but never actually established
// (or vice versa). Before 7.5.4 there is no gate and Pause() interrupts the render instead, so the equivalent
// observable is the render status.
bool ArnoldIsPaused(AtRenderSession* session)
{
#if ARNOLD_VERSION_NUM >= 70504
    return AiRenderIsPaused(session);
#else
    return AiRenderGetStatus(session) == AI_RENDER_STATUS_PAUSED;
#endif
}

// Builds a small, deliberately expensive scene directly with AiNode() calls (see the
// top-of-file comment for why this doesn't load a .usda file instead). The high
// AA_samples/resolution keep the render busy long enough for the Pause()/Interrupt()
// sequence in main() to reliably land while the render is still in flight. Do not lower
// these to "optimize" the test -- that would make it flaky. main() drops AA_samples once
// it is done with the pause checks, so the final convergence check stays quick.
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

        // Pausing is only advertised where Arnold provides the resumable AiRenderPause()/AiRenderResume() API.
#if ARNOLD_VERSION_NUM >= 70504
        Check(delegate.IsPauseSupported(), "IsPauseSupported() should be true with Arnold 7.5.4+");
#else
        Check(!delegate.IsPauseSupported(), "IsPauseSupported() should be false before Arnold 7.5.4");
#endif
        Check(!IsPaused(delegate, renderParam), "IsPaused() should be false on a fresh delegate");
        Check(!IsStopped(delegate, renderParam) || AiRenderGetStatus(session) == AI_RENDER_STATUS_NOT_STARTED,
            "IsStopped() should only report a not-yet-started render on a fresh delegate");

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
            // Arnold's AiRenderPause() gate (Arnold 7.5.4+) has no mechanism to preserve
            // accumulated samples across a scene edit, so any edit forces a real
            // interrupt+restart, which must be reflected in IsPaused() rather than left
            // claiming "paused" while the render is actually active again.
            delegate.Pause();
            Check(IsPaused(delegate, renderParam), "IsPaused() should be true right after Pause()");
            // Assert against Arnold's own view too, not just our bookkeeping: a gated render keeps reporting
            // AI_RENDER_STATUS_RENDERING, so a status check alone cannot tell a paused render from a running one
            // and would pass whether or not Pause() actually did anything.
            Check(ArnoldIsPaused(session), "Arnold does not report the render as paused after Pause()");

            renderParam->Interrupt(); // stand-in for a camera/mesh/light Sync() edit
            Check(!IsPaused(delegate, renderParam),
                "IsPaused() stayed true after a scene-edit Interrupt() cancelled the pause (#2719)");
            Check(!ArnoldIsPaused(session), "The interrupt did not unpark the render gated by AiRenderPause()");

            Check(WaitForStatus(renderParam, session, AI_RENDER_STATUS_RENDERING, 5000) && !ArnoldIsPaused(session),
                "Render did not resume after the edit-cancelled pause");

            // --- Resume() must clear the flag when there was no edit in between ---
            delegate.Pause();
            Check(IsPaused(delegate, renderParam), "IsPaused() should be true right after a second Pause()");
            delegate.Resume();
            Check(!IsPaused(delegate, renderParam), "IsPaused() should be false right after Resume()");
            // Pumped rather than checked immediately: on Arnold 7.5.4+ Resume() lifts the gate itself, but on the
            // interrupt-based fallback path the AiRenderResume() call lives in UpdateRender()'s PAUSED branch.
            Check(WaitForStatus(renderParam, session, AI_RENDER_STATUS_RENDERING, 5000) && !ArnoldIsPaused(session),
                "Render did not resume after Resume()");

            // --- Stop() must clear the pause flag, and must actually keep the render stopped ---
            delegate.Pause();
            Check(IsPaused(delegate, renderParam), "IsPaused() should be true right after a third Pause()");
#if PXR_VERSION >= 2203
            delegate.Stop(true);
#else
            delegate.Stop();
#endif
            Check(!IsPaused(delegate, renderParam), "IsPaused() stayed true after Stop() (#2719)");
            Check(IsStopped(delegate, renderParam), "IsStopped() should be true right after Stop()");
            // The regression this guards: Stop() only interrupts the render, which leaves the session at
            // AI_RENDER_STATUS_PAUSED -- and UpdateRender()'s PAUSED branch resumes anything it finds sitting
            // there. Without a latched stop the render therefore restarts by itself on the very next tick, with no
            // host involvement at all, while IsStopSupported() still claims stopping works.
            bool stayedStopped = true;
            for (int i = 0; i < 20 && stayedStopped; ++i) {
                renderParam->UpdateRender();
                stayedStopped = IsStopped(delegate, renderParam) &&
                    AiRenderGetStatus(session) != AI_RENDER_STATUS_RENDERING &&
                    AiRenderGetStatus(session) != AI_RENDER_STATUS_RESTARTING;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Check(stayedStopped, "Render restarted on its own after Stop(), without a Restart() (#2719)");

            // A scene edit must not resurrect a stopped render either.
            renderParam->Interrupt();
            for (int i = 0; i < 20 && stayedStopped; ++i) {
                renderParam->UpdateRender();
                stayedStopped = IsStopped(delegate, renderParam) &&
                    AiRenderGetStatus(session) != AI_RENDER_STATUS_RENDERING &&
                    AiRenderGetStatus(session) != AI_RENDER_STATUS_RESTARTING;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Check(stayedStopped, "A scene edit restarted a stopped render, without a Restart() (#2719)");
        }

        // Let the render actually finish so the delegate tears down cleanly, and check Restart() lifts the stop.
        // The heavy AA_samples above exist purely to keep the render in flight long enough for the checks above to
        // land; converging at that quality takes ~8s on a 10-core M1 Max and Restart() starts over from scratch, so
        // trim it back rather than making the timeout below absorb a loaded CI machine's slowdown.
        AiNodeSetInt(AiUniverseGetOptions(delegate.GetUniverse()), AtString("AA_samples"), 3);
        delegate.Restart();
        const auto finalStatus = RunToCompletion(renderParam, 60000);
        Check(finalStatus == HdArnoldRenderParam::Status::Converged, "Render did not converge by the end of the test");
    }

    AiEnd();
    return g_success ? 0 : 1;
}
