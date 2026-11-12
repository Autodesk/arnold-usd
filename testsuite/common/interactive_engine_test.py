"""Framework for the interactive render delegate tests in python: a test describes a scenario, i.e. a UsdStage and a
list of steps, each editing the stage and followed by a render whose image is checked. The scenario runs the
HdArnoldRendererPlugin render delegate through a headless UsdImagingGL.Engine (gpuEnabled=False), as a viewport does:
Render() is called until the render converges, and the edits can also be done while it renders.

It mirrors the C++ framework (interactive_usd_test.h), with only its RenderDelegate method, so that its scenarios port
almost line by line, and adds what the engine drives: AOV, time, purposes, edits during the render, pause/resume and
stop/restart. The testsuite pass selects Hydra 1 or 2 (USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX), see the modes.

A test only needs a data/test.py calling Run(), and in its README PARAMS: {'script': ...} running it with the python
interpreter of the USD being tested. The environment it reads:
  - TESTSUITE_PXR_PLUGINPATH_NAME or PREFIX_TEST_RENDERDELEGATE: folders of the arnold USD plugins (bundle, schemas),
    registered in the PlugRegistry, as the name of the plugin path variable is a USD build setting (e.g.
    ARNOLD_PXR_PLUGINPATH_NAME). If neither is set, the plugins must already be found by USD.
  - INTERACTIVE_ENGINE_READBACK_PATH: the folder of the interactive_engine_readback module (libs/test_support),
    installed under <install>/testsuite/python. Otherwise it must be on the PYTHONPATH.
  - USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX: Hydra 2 if not 0, the USD default otherwise.

Every render is saved as delegate_step<N>.tif in the current folder, except the render of the reference step (the last
one by default), saved as testrender.tif to be compared against the reference image.
"""
import array
import ctypes
import os
import sys
import time

# Modes, depending on the testsuite pass. The bits are the same as in interactive_usd_test.h.
DELEGATE_HYDRA1 = 1 << 3  # "usd" and "hydra" passes
DELEGATE_HYDRA2 = 1 << 4  # "hydra2" pass
ANY_DELEGATE = DELEGATE_HYDRA1 | DELEGATE_HYDRA2
ANY_MODE = ANY_DELEGATE

WINDOWS = 1 << 0
LINUX = 1 << 1
DARWIN = 1 << 2
ALL_PLATFORMS = WINDOWS | LINUX | DARWIN

# Step actions, see Step
PAUSE_RESUME = 'pause_resume'
STOP_RESTART = 'stop_restart'

RENDERER_PLUGIN = 'HdArnoldRendererPlugin'
# Seconds for a render to converge. The first one includes the licence check, which can take more than 100s.
FIRST_RENDER_TIMEOUT = 300.0
RENDER_TIMEOUT = 60.0

_LOG_PREFIX = '[interactive_engine_test] '


class Color(object):
    """What a pixel is expected to look like, see ExpectColor"""
    RED = 'red'
    GREEN = 'green'
    BLUE = 'blue'
    GREY = 'grey'
    BLACK = 'black'
    EMPTY = 'empty'


class KnownIssue(object):
    """A check known to fail in some modes. It is then reported as a warning instead of failing the test, and a warning
    also tells when it passes, so the known issue can be removed once fixed. When the reference step is affected, the
    per-pass reference image of these modes (e.g. ref/reference.hydra2.tif) is rendered with the issue."""

    def __init__(self, modes, description):
        self.modes = modes
        self.description = description


class SkippedModes(object):
    """Modes in which a scenario isn't run on some platforms, e.g. because it crashes there. The reason is logged as a
    warning. Unlike the C++ framework, there is no other method to write the render compared against the reference
    image, so the runner then fails the test if it has a reference image: rather skip the test in that pass or on that
    platform through testsuite/groups (pass_<name>, the OS groups)."""

    def __init__(self, modes, reason, platforms=ALL_PLATFORMS):
        self.modes = modes
        self.reason = reason
        self.platforms = platforms


class Step(object):
    """A step of a scenario: what's changed before its render, and the check of the image.

    - edit(stage): edits the stage before the render, nothing is edited if None (e.g. for the initial render)
    - check(image): returns a description of the failure, or an empty string, see ExpectColor and AllOf
    - known_issues: list of KnownIssue
    - framing: CameraUtil.Framing sent to the engine before the render, kept by the next steps
    - buffer_size: (width, height) of the render buffer, kept by the next steps. It's the size of the images checked.
    - render_settings: {name: value} renderer settings set before the render, e.g. {'arnold:global:AA_samples': 1}
    - window_policy: CameraUtil.ConformWindowPolicy overriding the camera's, kept by the next steps
    And what the engine adds:
    - aov: renderer AOV rendered and checked from this step on, e.g. 'depth' ('color' initially)
    - time: Usd.TimeCode or frame number rendered from this step on (Usd.TimeCode.Default() initially)
    - purposes: the purposes shown from this step on, a subset of ('proxy', 'render', 'guide') (('proxy',) initially,
      as the default UsdImagingGL.RenderParams)
    - during_render(stage): an edit done once the render after the edit has started and before it converges
      (0 < percentDone < 100). The step must change something else (edit, settings...) for a render to start: with
      nothing else, the render is already converged. If the render converges before, the step is reported as not
      tested during the render.
    - action: PAUSE_RESUME, to pause the render, do the edit and resume, or STOP_RESTART, to stop the render, do the
      edit and restart. The renderer must support it.
    """

    def __init__(self, description, edit=None, check=None, known_issues=(), framing=None, buffer_size=None,
                 render_settings=None, window_policy=None, aov=None, time=None, purposes=None, during_render=None,
                 action=None):
        if action not in (None, PAUSE_RESUME, STOP_RESTART):
            raise ValueError('Unknown action {!r}'.format(action))
        self.description = description
        self.edit = edit
        self.check = check
        self.known_issues = list(known_issues)
        self.framing = framing
        self.buffer_size = buffer_size
        self.render_settings = dict(render_settings or {})
        self.window_policy = window_policy
        self.aov = aov
        self.time = time
        self.purposes = purposes
        self.during_render = during_render
        self.action = action


class Scenario(object):
    """A stage and the steps run on it.

    - create_stage(): creates the stage
    - camera: path of the camera the stage is rendered from
    - width, height: the size of the render buffer and of the images checked, unless buffer_width/buffer_height are set
    - framing: CameraUtil.Framing initially sent to the engine, the buffer's full rect if None
    - usd_override_double_sided: the Arnold option, set it to False to check the sidedness of meshes
    - reference_step: index of the step whose render is compared against the reference image, the last one if < 0
    - skipped_modes: list of SkippedModes
    """

    def __init__(self, create_stage, steps, camera='/World/camera', width=160, height=120, aa_samples=3,
                 buffer_width=0, buffer_height=0, framing=None, usd_override_double_sided=True, reference_step=-1,
                 skipped_modes=()):
        self.create_stage = create_stage
        self.steps = list(steps)
        self.camera = camera
        self.width = width
        self.height = height
        self.aa_samples = aa_samples
        self.buffer_width = buffer_width
        self.buffer_height = buffer_height
        self.framing = framing
        self.usd_override_double_sided = usd_override_double_sided
        self.reference_step = reference_step
        self.skipped_modes = list(skipped_modes)


class Image(object):
    """A rendered AOV as RGBA floats, top-down. Positions are normalized, (0, 0) being the top left corner."""

    def __init__(self, width, height, rgba, format=''):
        self.width = width
        self.height = height
        self.rgba = rgba
        # Format of the render buffer, e.g. float32x4
        self.format = format

    def Average(self, u, v, radius=2):
        """Average RGBA of the pixels around a position"""
        cx = min(max(int(u * self.width), 0), self.width - 1)
        cy = min(max(int(v * self.height), 0), self.height - 1)
        total, count = [0.0] * 4, 0
        for y in range(max(cy - radius, 0), min(cy + radius, self.height - 1) + 1):
            for x in range(max(cx - radius, 0), min(cx + radius, self.width - 1) + 1):
                i = (y * self.width + x) * 4
                for c in range(4):
                    total[c] += self.rgba[i + c]
                count += 1
        return [t / count for t in total] if count else total


def _Position(u, v):
    # A position given as a single (u, v) sequence, e.g. returned by ScreenPosition
    if v is None:
        return float(u[0]), float(u[1])
    return float(u), float(v)


def ExpectColor(color, u=0.5, v=None):
    """Checks the color around a position, given as u, v or as a single (u, v), the center by default. RED, GREEN, BLUE
    and GREY are lit opaque surfaces, with that dominant color or a neutral one, BLACK is an opaque unlit surface and
    EMPTY is the background. The same thresholds as ExpectColor in interactive_usd_test.cpp."""
    if v is None and not hasattr(u, '__len__'):
        v = 0.5
    u, v = _Position(u, v)

    def check(image):
        c = image.Average(u, v)
        max_rgb, min_rgb = max(c[:3]), min(c[:3])
        if color in (Color.RED, Color.GREEN, Color.BLUE):
            dominant = (Color.RED, Color.GREEN, Color.BLUE).index(color)
            ok = c[3] > 0.9 and c[dominant] > 0.05 and all(c[dominant] > 3.0 * c[i] for i in range(3) if i != dominant)
        elif color == Color.GREY:
            ok = c[3] > 0.9 and min_rgb > 0.05 and max_rgb < 1.5 * min_rgb
        elif color == Color.BLACK:
            ok = c[3] > 0.9 and max_rgb < 0.02
        elif color == Color.EMPTY:
            ok = c[3] < 0.1
        else:
            raise ValueError('Unknown color {!r}'.format(color))
        if ok:
            return ''
        return 'expected {} at ({:.2f}, {:.2f}), got ({:.4f}, {:.4f}, {:.4f}, {:.4f})'.format(color, u, v, *c)

    return check


def AllOf(*checks):
    """Runs all the checks, given as arguments or as a single list"""
    if len(checks) == 1 and isinstance(checks[0], (list, tuple)):
        checks = checks[0]

    def check(image):
        return '; '.join(error for error in (c(image) for c in checks) if error)

    return check


# Logging, through the Arnold log once it's started so that the messages are in order with it

_success = True


def _Log(level, message):
    import arnold
    if arnold.AiArnoldIsActive():
        function = {'info': arnold.AiMsgInfo, 'warning': arnold.AiMsgWarning, 'error': arnold.AiMsgError}[level]
        function((_LOG_PREFIX + message).replace('%', '%%'))
    else:
        stream = sys.stderr if level != 'info' else sys.stdout
        stream.write('{}{}{}\n'.format(_LOG_PREFIX, '' if level == 'info' else level.upper() + ': ', message))
        stream.flush()


def _Info(message):
    _Log('info', message)


def _Warning(message):
    _Log('warning', message)


def _Check(condition, message):
    global _success
    if not condition:
        _Log('error', message)
        _success = False
    return condition


class _TestError(Exception):
    """Stops the scenario, its message being reported as an error"""


# Environment

def _PathList(value):
    return [path for path in (value or '').split(os.pathsep) if path]


def _SetupDllDirectories():
    """Since python 3.8, Windows doesn't search the PATH for the DLLs of the extension modules (USD's .pyd, the readback
    module): the folders of the PATH and of the arnold plugins are added as DLL directories."""
    if sys.platform != 'win32' or not hasattr(os, 'add_dll_directory'):
        return []
    folders = _PathList(os.environ.get('PATH')) + _PathList(os.environ.get('PREFIX_TEST_RENDERDELEGATE')) + \
        _PathList(os.environ.get('TESTSUITE_PXR_PLUGINPATH_NAME'))
    handles = []
    for folder in folders:
        if os.path.isdir(folder):
            try:
                handles.append(os.add_dll_directory(os.path.abspath(folder)))
            except OSError:
                pass
    return handles


def _RegisterPlugins():
    """Registers the arnold USD plugins in the PlugRegistry, then checks the engine finds the renderer plugin"""
    from pxr import Plug, UsdImagingGL
    # The same precedence as the runner for the plugin path (tools/test/testsuite.py): the cmake testsuite target sets
    # TESTSUITE_PXR_PLUGINPATH_NAME to its install, PREFIX_TEST_RENDERDELEGATE being the SCons install then
    folders = _PathList(os.environ.get('TESTSUITE_PXR_PLUGINPATH_NAME')) or \
        _PathList(os.environ.get('PREFIX_TEST_RENDERDELEGATE'))
    if folders:
        Plug.Registry().RegisterPlugins(folders)
    plugins = [str(plugin) for plugin in UsdImagingGL.Engine.GetRendererPlugins()]
    if RENDERER_PLUGIN not in plugins:
        raise _TestError('The {} renderer plugin isn\'t registered, from {} (renderer plugins found: {})'.format(
            RENDERER_PLUGIN, os.pathsep.join(folders) if folders else 'the USD plugin path', ', '.join(plugins)))


def _ImportReadback():
    folder = os.environ.get('INTERACTIVE_ENGINE_READBACK_PATH')
    if folder and folder not in sys.path:
        sys.path.insert(0, folder)
    try:
        import interactive_engine_readback
    except ImportError as error:
        raise _TestError('Could not import the interactive_engine_readback module (INTERACTIVE_ENGINE_READBACK_PATH={}):'
                         ' {}'.format(folder, error))
    return interactive_engine_readback


def _SceneIndexEnabled():
    from pxr import Tf
    try:
        return bool(Tf.GetEnvSetting('USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX'))
    except Exception:
        return os.environ.get('USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX', '0').strip() != '0'


def _CurrentMode():
    return DELEGATE_HYDRA2 if _SceneIndexEnabled() else DELEGATE_HYDRA1


def _ModeName(mode):
    return {DELEGATE_HYDRA1: 'render delegate with hydra 1', DELEGATE_HYDRA2: 'render delegate with hydra 2'}.get(
        mode, 'unknown mode')


def _CurrentPlatform():
    if sys.platform == 'win32':
        return WINDOWS
    if sys.platform == 'darwin':
        return DARWIN
    return LINUX


# The renderer

class _EngineRenderer(object):
    """The arnold render delegate driven by a UsdImagingGL.Engine, as a viewport does"""

    def __init__(self, scenario, stage, readback):
        from pxr import CameraUtil, Gf, Sdf, Usd, UsdImagingGL
        self._stage = stage
        self._readback = readback
        parameters = UsdImagingGL.Engine.Parameters()
        parameters.rendererPluginId = RENDERER_PLUGIN
        parameters.gpuEnabled = False
        self.engine = UsdImagingGL.Engine(parameters)
        if self.engine.GetCurrentRendererId() != RENDERER_PLUGIN:
            raise _TestError('The engine uses the renderer {} instead of {}'.format(
                self.engine.GetCurrentRendererId(), RENDERER_PLUGIN))
        # The render delegate sets its own log flags
        import arnold
        arnold.AiMsgSetConsoleFlags(None, arnold.AI_LOG_INFO | arnold.AI_LOG_WARNINGS | arnold.AI_LOG_ERRORS)

        self.engine.SetRendererSetting('arnold:global:AA_samples', scenario.aa_samples)
        self.engine.SetRendererSetting('arnold:global:usd_override_double_sided', scenario.usd_override_double_sided)
        self.engine.SetCameraPath(Sdf.Path(scenario.camera))
        self.aov = 'color'
        self.engine.SetRendererAov(self.aov)
        width = scenario.buffer_width if scenario.buffer_width > 0 else scenario.width
        height = scenario.buffer_height if scenario.buffer_height > 0 else scenario.height
        self.SetBufferSize(width, height)
        self.SetFraming(scenario.framing or CameraUtil.Framing(Gf.Rect2i(Gf.Vec2i(0, 0), width, height)))
        self.params = UsdImagingGL.RenderParams()
        self.params.clearColor = Gf.Vec4f(0, 0, 0, 0)
        self.params.frame = Usd.TimeCode.Default()
        self._first_render = True

    def SetBufferSize(self, width, height):
        from pxr import Gf
        self.buffer_size = (int(width), int(height))
        self.engine.SetRenderBufferSize(Gf.Vec2i(*self.buffer_size))

    def SetFraming(self, framing):
        self.engine.SetFraming(framing)

    def SetWindowPolicy(self, policy):
        self.engine.SetOverrideWindowPolicy(policy)

    def SetAov(self, aov):
        if not self.engine.SetRendererAov(aov):
            raise _TestError('The renderer doesn\'t support the AOV {} (AOVs: {})'.format(
                aov, ', '.join(str(a) for a in self.engine.GetRendererAovs())))
        self.aov = aov

    def SetTime(self, time):
        from pxr import Usd
        self.params.frame = time if isinstance(time, Usd.TimeCode) else Usd.TimeCode(time)

    def SetPurposes(self, purposes):
        purposes = set(purposes)
        unknown = purposes - {'proxy', 'render', 'guide'}
        if unknown:
            raise ValueError('Unknown purposes {}'.format(', '.join(sorted(unknown))))
        self.params.showProxy = 'proxy' in purposes
        self.params.showRender = 'render' in purposes
        self.params.showGuides = 'guide' in purposes

    def RenderOnce(self):
        """A single Render() call, as a viewport does on each redraw. Returns whether the render is converged."""
        self.engine.Render(self._stage.GetPseudoRoot(), self.params)
        return self.engine.IsConverged()

    def PercentDone(self):
        return float(self.engine.GetRenderStats().get('percentDone', 100.0))

    def StartRender(self):
        """Calls Render() until the render has started but isn't done. Returns False if it converged before."""
        start = time.time()
        while True:
            if self.RenderOnce():
                return False
            if 0.0 < self.PercentDone() < 100.0:
                return True
            if time.time() - start > self._Timeout():
                raise _TestError('The render did not start in {:.0f}s'.format(self._Timeout()))
            time.sleep(0.001)

    def Converge(self):
        """Calls Render() until the render converges"""
        start = time.time()
        while not self.RenderOnce():
            if time.time() - start > self._Timeout():
                raise _TestError('The render did not converge in {:.0f}s'.format(self._Timeout()))
            time.sleep(0.005)
        self._first_render = False

    def ReadImage(self):
        width, height, format, data = self._readback.ReadAov(self.engine, self.aov)
        # The buffer belongs to the host, the render delegate must not resize it, whatever the framing
        if (width, height) != self.buffer_size:
            raise _TestError('The {} render buffer is {}x{}, it should be {}x{}'.format(
                self.aov, width, height, *self.buffer_size))
        rgba = array.array('f')
        rgba.frombytes(data)
        return Image(width, height, rgba, format)

    def _Timeout(self):
        return FIRST_RENDER_TIMEOUT if self._first_render else RENDER_TIMEOUT


def _WriteImage(filename, image):
    """Writes the image as a float TIFF through Arnold, as the C++ framework does"""
    import arnold
    size = len(image.rgba) * image.rgba.itemsize
    # The buffer is allocated by Arnold, as in DelegateRenderer::Render of interactive_usd_test.cpp
    buffer = arnold.AiMalloc(size)
    ctypes.memmove(buffer, image.rgba.buffer_info()[0], size)
    at_image = arnold.AtImage()
    at_image.buffer = buffer
    at_image.width = at_image.full_width = image.width
    at_image.height = at_image.full_height = image.height
    at_image.x = at_image.y = 0
    at_image.channels = 4
    at_image.format = arnold.AI_TYPE_FLOAT
    _Check(arnold.AiWriteImage(filename, ctypes.byref(at_image)), 'Could not write ' + filename)


def _RenderStep(renderer, stage, step, prefix):
    for name, value in step.render_settings.items():
        renderer.engine.SetRendererSetting(name, value)
    if step.buffer_size is not None:
        renderer.SetBufferSize(*step.buffer_size)
    if step.framing is not None:
        renderer.SetFraming(step.framing)
    if step.window_policy is not None:
        renderer.SetWindowPolicy(step.window_policy)
    if step.aov is not None:
        renderer.SetAov(step.aov)
    if step.time is not None:
        renderer.SetTime(step.time)
    if step.purposes is not None:
        renderer.SetPurposes(step.purposes)

    engine = renderer.engine
    if step.action == PAUSE_RESUME:
        if not engine.IsPauseRendererSupported():
            raise _TestError('The renderer doesn\'t support pausing')
        renderer.RenderOnce()
        _Check(engine.PauseRenderer(), prefix + ': PauseRenderer failed')
        if step.edit:
            step.edit(stage)
        renderer.RenderOnce()
        _Check(engine.ResumeRenderer(), prefix + ': ResumeRenderer failed')
    elif step.action == STOP_RESTART:
        if not engine.IsStopRendererSupported():
            raise _TestError('The renderer doesn\'t support stopping')
        renderer.RenderOnce()
        _Check(engine.StopRenderer(), prefix + ': StopRenderer failed')
        if step.edit:
            step.edit(stage)
        _Check(engine.RestartRenderer(), prefix + ': RestartRenderer failed')
    elif step.edit:
        step.edit(stage)

    if step.during_render:
        if renderer.StartRender():
            _Info('{}: edit done at {:.0f}% of the render'.format(prefix, renderer.PercentDone()))
        else:
            _Warning('{}: the render converged before the edit, it was not tested during the render'.format(prefix))
        step.during_render(stage)
    renderer.Converge()
    return renderer.ReadImage()


def _RunScenario(scenario):
    mode = _CurrentMode()
    mode_name = _ModeName(mode)
    _Info('running the scenario with the ' + mode_name)
    stage = scenario.create_stage()
    if not _Check(bool(stage), 'Could not create the stage'):
        return
    readback = _ImportReadback()
    renderer = _EngineRenderer(scenario, stage, readback)
    try:
        reference_step = scenario.reference_step if scenario.reference_step >= 0 else len(scenario.steps) - 1
        for i, step in enumerate(scenario.steps):
            prefix = '{}, step {} ({})'.format(mode_name, i, step.description)
            _Info('{}, step {}: {}'.format(mode_name, i, step.description))
            image = _RenderStep(renderer, stage, step, prefix)
            filename = 'testrender.tif' if i == reference_step else 'delegate_step{}.tif'.format(i)
            _WriteImage(filename, image)
            error = step.check(image) if step.check else ''

            known_issue = next((issue for issue in step.known_issues if issue.modes & mode), None)
            if known_issue is None:
                _Check(not error, '{}: {}'.format(prefix, error))
            elif error:
                _Warning('{}: {}, known issue: {}'.format(prefix, error, known_issue.description))
            else:
                _Warning('{} now passes, known issue "{}" might be fixed'.format(prefix, known_issue.description))
    finally:
        # The engine, and the render delegate with it, are released before AiEnd
        del renderer.engine
        del renderer


def Run(scenario):
    """Runs the scenario in the current mode, returning the program exit code"""
    global _success
    _success = True
    dll_directories = _SetupDllDirectories()  # noqa: F841, kept until the end
    # libai must be loaded before the bundle, which has no rpath to it
    import arnold
    began = False
    try:
        from pxr import Usd  # noqa: F401, loads libusd before the readback module
        _RegisterPlugins()
        # Arnold is started here rather than by the render delegate, so that it can be ended once the engine is
        # released, as the C++ framework does
        if not arnold.AiArnoldIsActive():
            arnold.AiBegin(arnold.AI_SESSION_INTERACTIVE)
            began = True
        arnold.AiMsgSetConsoleFlags(None, arnold.AI_LOG_INFO | arnold.AI_LOG_WARNINGS | arnold.AI_LOG_ERRORS)

        mode = _CurrentMode()
        skipped = next((s for s in scenario.skipped_modes if s.modes & mode), None)
        if skipped is not None and skipped.platforms & _CurrentPlatform():
            _Warning('not running the scenario with the {}: {}'.format(_ModeName(mode), skipped.reason))
        else:
            _RunScenario(scenario)
    except _TestError as error:
        _Check(False, str(error))
    finally:
        import gc
        gc.collect()
        if began:
            arnold.AiEnd()
    return 0 if _success else 1


# Helpers to build and edit stages, the same as in interactive_usd_test.h

def DefineCamera(stage, path, position):
    """A camera at the given position, looking down -Z, with a 54.43 degrees horizontal field of view"""
    from pxr import Gf, UsdGeom
    camera = UsdGeom.Camera.Define(stage, path)
    UsdGeom.XformCommonAPI(camera.GetPrim()).SetTranslate(Gf.Vec3d(*position))
    camera.CreateFocalLengthAttr().Set(35.0)
    camera.CreateHorizontalApertureAttr().Set(36.0)
    camera.CreateVerticalApertureAttr().Set(27.0)
    return camera


def ScreenPosition(point, camera_position=(0, 0, 6)):
    """Normalized image position (u, v) of a world position, as seen by a camera made by DefineCamera at camera_position,
    with the default 160x120 resolution"""
    # Half the horizontal aperture over the focal length set by DefineCamera, and the 160x120 aspect ratio
    tan_half_fov = 18.0 / 35.0
    distance = camera_position[2] - point[2]
    half_width = distance * tan_half_fov
    half_height = half_width * 120.0 / 160.0
    return (0.5 + 0.5 * (point[0] - camera_position[0]) / half_width,
            0.5 - 0.5 * (point[1] - camera_position[1]) / half_height)


def FramedScreenPosition(point, framing, buffer_width, buffer_height, camera_position=(0, 0, 6)):
    """Normalized position (u, v), in a buffer_width x buffer_height render buffer, of a world position as seen by a
    camera made by DefineCamera at camera_position, when the host sends this framing: the display window gives the film
    its position and size in the buffer (CameraUtil.DontConform). It can be outside of [0, 1]."""
    distance = camera_position[2] - point[2]
    half_width = distance * 18.0 / 35.0
    half_height = half_width * 27.0 / 36.0
    ndc_x = (point[0] - camera_position[0]) / half_width
    ndc_y = (point[1] - camera_position[1]) / half_height
    display = framing.displayWindow
    size = display.GetSize()
    return ((display.GetMin()[0] + size[0] * (0.5 + 0.5 * ndc_x)) / buffer_width,
            (display.GetMin()[1] + size[1] * (0.5 - 0.5 * ndc_y)) / buffer_height)


def DefineDistantLight(stage, path, intensity=3.0):
    """A normalized distant light, pointing down -Z and tilted by 40 degrees around X and Y"""
    from pxr import Gf, UsdGeom, UsdLux
    light = UsdLux.DistantLight.Define(stage, path)
    # Tilted, so that its specular highlight isn't in the center of the surfaces facing the camera
    UsdGeom.XformCommonAPI(light.GetPrim()).SetRotate(Gf.Vec3f(40.0, 40.0, 0.0))
    light.CreateIntensityAttr().Set(intensity)
    # Without normalize, the intensity of a distant light is spread over its (0.53 degrees) angle
    light.CreateNormalizeAttr().Set(True)
    return light


def DefinePreviewMaterial(stage, path, diffuse_color):
    """A UsdPreviewSurface material, whose shader is <path>/surface"""
    from pxr import Gf, Sdf, UsdShade
    path = Sdf.Path(path)
    material = UsdShade.Material.Define(stage, path)
    shader = UsdShade.Shader.Define(stage, path.AppendChild('surface'))
    shader.CreateIdAttr('UsdPreviewSurface')
    shader.CreateInput('diffuseColor', Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(*diffuse_color))
    shader.CreateInput('roughness', Sdf.ValueTypeNames.Float).Set(0.5)
    material.CreateSurfaceOutput().ConnectToSource(shader.ConnectableAPI(), 'surface')
    return material


def BindMaterial(stage, prim_path, material):
    from pxr import UsdShade
    UsdShade.MaterialBindingAPI.Apply(stage.GetPrimAtPath(prim_path)).Bind(material)


def SetTranslate(stage, path, translate):
    from pxr import Gf, UsdGeom
    UsdGeom.XformCommonAPI(stage.GetPrimAtPath(path)).SetTranslate(Gf.Vec3d(*translate))
