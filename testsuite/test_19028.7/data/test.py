# Time changes: the frame of the render params of the engine is changed between the renders, see the time of Step in
# interactive_engine_test.py
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    AllOf, BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor, Run, Scenario,
    ScreenPosition, Step)

SPHERE = '/World/sphere'
left = ScreenPosition((-1.5, 0, 0))
right = ScreenPosition((1.5, 0, 0))


def create_stage():
    """The sphere moves from the left to the right between the frames 1 and 11, and turns from red to blue between
    the frames 5 and 7"""
    from pxr import Gf, Usd, UsdGeom, UsdShade
    stage = Usd.Stage.CreateInMemory()
    stage.SetStartTimeCode(1)
    stage.SetEndTimeCode(11)
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, '/World/camera', (0, 0, 6))
    DefineDistantLight(stage, '/World/light')
    sphere = UsdGeom.Sphere.Define(stage, SPHERE)
    sphere.CreateRadiusAttr().Set(1.0)
    translate = sphere.AddTranslateOp()
    translate.Set(Gf.Vec3d(-1.5, 0, 0), 1)
    translate.Set(Gf.Vec3d(1.5, 0, 0), 11)
    BindMaterial(stage, SPHERE, DefinePreviewMaterial(stage, '/World/materials/sphere_mat', (0.8, 0.05, 0.05)))
    color = UsdShade.Shader.Get(stage, '/World/materials/sphere_mat/surface').GetInput('diffuseColor')
    for frame, rgb in ((1, (0.8, 0.05, 0.05)), (5, (0.8, 0.05, 0.05)), (7, (0.05, 0.05, 0.8)), (11, (0.05, 0.05, 0.8))):
        color.Set(Gf.Vec3f(*rgb), frame)
    return stage


def expect_opaque_center(image):
    """At frame 6, the sphere is at the center, with a color between red and blue"""
    alpha = image.Average(0.5, 0.5)[3]
    return '' if alpha > 0.9 else 'expected the sphere at the center, got an alpha of {:.3f}'.format(alpha)


at_frame_1 = AllOf(ExpectColor(Color.RED, left), ExpectColor(Color.EMPTY, right))
at_frame_11 = AllOf(ExpectColor(Color.EMPTY, left), ExpectColor(Color.BLUE, right))

scenario = Scenario(create_stage, [
    Step('frame 1', time=1, check=at_frame_1),
    Step('frame 11', time=11, check=at_frame_11),
    Step('frame 6, between the time samples', time=6,
         check=AllOf(ExpectColor(Color.EMPTY, left), ExpectColor(Color.EMPTY, right), expect_opaque_center)),
    Step('back to frame 1', time=1, check=at_frame_1),
    Step('frame 11 again', time=11, check=at_frame_11),
])

sys.exit(Run(scenario))
