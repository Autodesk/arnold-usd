# Host framing, render buffer size and window policy changes, as test_2819 to test_2819.6 do with the C++ framework
# (see testsuite/common/framing_test_utils.h, whose checks are ported here)
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    AllOf, BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor,
    FramedScreenPosition, Run, Scenario, Step)

CAMERA = '/World/camera'
SPHERE_RADIUS = 1.2


def create_stage():
    from pxr import Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, CAMERA, (0, 0, 6))
    DefineDistantLight(stage, '/World/light')
    UsdGeom.Sphere.Define(stage, '/World/sphere').CreateRadiusAttr().Set(SPHERE_RADIUS)
    BindMaterial(stage, '/World/sphere',
                 DefinePreviewMaterial(stage, '/World/materials/sphere_mat', (0.8, 0.05, 0.05)))
    return stage


def framing(display_min, display_max, data_min, data_size):
    from pxr import CameraUtil, Gf
    return CameraUtil.Framing(Gf.Range2f(Gf.Vec2f(*display_min), Gf.Vec2f(*display_max)),
                              Gf.Rect2i(Gf.Vec2i(*data_min), *data_size))


def full_framing(width, height):
    return framing((0, 0), (width, height), (0, 0), (width, height))


def expect_projected_sphere(project, pixels_per_unit, data, buffer_width, buffer_height):
    """Checks the pixels at a grid of world positions in the plane of the sphere center, projected to normalized buffer
    positions by project: the sphere where it's expected in the data window, the background around it, and nothing
    outside of the data window. The same as ExpectProjectedSphere in framing_test_utils.h."""
    # The radius of the sphere silhouette in the plane of its center
    silhouette = SPHERE_RADIUS * 6.0 / (36.0 - SPHERE_RADIUS * SPHERE_RADIUS) ** 0.5
    margin = 4.0
    min_x, min_y = data.GetMinX(), data.GetMinY()
    max_x, max_y = data.GetMaxX() + 1, data.GetMaxY() + 1
    checks = []
    offsets = (-2.4, -1.7, -0.4, 0.0, 0.4, 1.7, 2.4)
    for x in offsets:
        for y in offsets:
            position = project((x, y, 0))
            px, py = position[0] * buffer_width, position[1] * buffer_height
            if px < margin or py < margin or px > buffer_width - margin or py > buffer_height - margin:
                continue
            inside = min_x + margin <= px <= max_x - margin and min_y + margin <= py <= max_y - margin
            outside = px <= min_x - margin or py <= min_y - margin or px >= max_x + margin or py >= max_y + margin
            distance = (x * x + y * y) ** 0.5
            if outside:
                checks.append(ExpectColor(Color.EMPTY, position))
            elif inside and distance <= 0.5:
                checks.append(ExpectColor(Color.RED, position))
            elif inside and (distance - silhouette) * pixels_per_unit >= margin:
                checks.append(ExpectColor(Color.EMPTY, position))
    return AllOf(checks)


def expect_sphere(framing, buffer_width=160, buffer_height=120):
    """The sphere seen by the 36x27 camera, the display window giving the film its position and size in the buffer"""
    pixels_per_unit = framing.displayWindow.GetSize()[0] / (2.0 * 6.0 * 18.0 / 35.0)
    return expect_projected_sphere(
        lambda point: FramedScreenPosition(point, framing, buffer_width, buffer_height), pixels_per_unit,
        framing.dataWindow, buffer_width, buffer_height)


def expect_conformed_sphere(framing, window_policy, horizontal_aperture, vertical_aperture, buffer_width=160,
                            buffer_height=120):
    """The sphere seen by a camera with this aperture, conformed to the display window with the window policy and
    framed by CameraUtil.Framing.ApplyToProjectionMatrix: what Storm draws. The same as ExpectConformedSphere in
    framing_test_utils.h."""
    from pxr import Gf
    camera = Gf.Camera()
    camera.focalLength = 35.0
    camera.horizontalAperture = horizontal_aperture
    camera.verticalAperture = vertical_aperture
    projection = framing.ApplyToProjectionMatrix(camera.frustum.ComputeProjectionMatrix(), window_policy)
    data = framing.dataWindow

    # The camera is at (0, 0, 6) looking down -Z, NDC maps to the data window, y down
    def project(point):
        clip = Gf.Vec4d(point[0], point[1], point[2] - 6.0, 1.0) * projection
        ndc_x, ndc_y = clip[0] / clip[3], clip[1] / clip[3]
        return ((data.GetMinX() + data.GetWidth() * (0.5 + 0.5 * ndc_x)) / buffer_width,
                (data.GetMinY() + data.GetHeight() * (0.5 - 0.5 * ndc_y)) / buffer_height)

    origin, unit_x, unit_y = project((0, 0, 0)), project((1, 0, 0)), project((0, 1, 0))
    pixels_per_unit = min(abs(unit_x[0] - origin[0]) * buffer_width, abs(unit_y[1] - origin[1]) * buffer_height)
    return expect_projected_sphere(project, pixels_per_unit, data, buffer_width, buffer_height)


def set_aperture(horizontal, vertical):
    def edit(stage):
        from pxr import UsdGeom
        camera = UsdGeom.Camera(stage.GetPrimAtPath(CAMERA))
        camera.GetHorizontalApertureAttr().Set(horizontal)
        camera.GetVerticalApertureAttr().Set(vertical)
    return edit


def main():
    from pxr import CameraUtil
    none = full_framing(160, 120)
    film_translate = framing((20, 10), (180, 130), (0, 0), (160, 120))
    overscan = framing((40, 30), (120, 90), (0, 0), (160, 120))
    zoom = framing((-80, -60), (240, 180), (0, 0), (160, 120))
    render_region = framing((0, 0), (160, 120), (80, 60), (80, 60))
    large = full_framing(320, 240)
    large_film_translate = framing((40, 20), (360, 260), (0, 0), (320, 240))
    scenario = Scenario(create_stage, [
        Step('no framing, the display window is the data window', check=expect_sphere(none)),
        Step('film translate', framing=film_translate, check=expect_sphere(film_translate)),
        Step('overscan, the film is half the size of the buffer', framing=overscan, check=expect_sphere(overscan)),
        Step('zoom, the film is twice the size of the buffer', framing=zoom, check=expect_sphere(zoom)),
        Step('render region, the data window is a quarter of the buffer', framing=render_region,
             check=expect_sphere(render_region)),
        Step('render buffer twice as large', buffer_size=(320, 240), framing=large,
             check=expect_sphere(large, 320, 240)),
        Step('film translate in the larger buffer', framing=large_film_translate,
             check=expect_sphere(large_film_translate, 320, 240)),
        Step('back to the initial buffer, no framing', buffer_size=(160, 120), framing=none,
             check=expect_sphere(none)),
        Step('16:9 camera, matched vertically', edit=set_aperture(36.0, 20.25),
             window_policy=CameraUtil.MatchVertically,
             check=expect_conformed_sphere(none, CameraUtil.MatchVertically, 36.0, 20.25)),
        Step('16:9 camera, matched horizontally', window_policy=CameraUtil.MatchHorizontally,
             check=expect_conformed_sphere(none, CameraUtil.MatchHorizontally, 36.0, 20.25)),
        Step('back to the 4:3 camera', edit=set_aperture(36.0, 27.0), check=expect_sphere(none)),
    ])
    return Run(scenario)


sys.exit(main())
