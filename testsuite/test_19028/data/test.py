import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor, Run, Scenario,
    ScreenPosition, Step)

SPHERE = '/World/sphere'
SURFACE = '/World/materials/sphere_mat/surface'


def create_stage():
    from pxr import Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, '/World/camera', (0, 0, 6))
    DefineDistantLight(stage, '/World/light')
    UsdGeom.Sphere.Define(stage, SPHERE).CreateRadiusAttr().Set(1.0)
    BindMaterial(stage, SPHERE, DefinePreviewMaterial(stage, '/World/materials/sphere_mat', (0.8, 0.05, 0.05)))
    return stage


def set_color(rgb):
    def edit(stage):
        from pxr import Gf, UsdShade
        UsdShade.Shader.Get(stage, SURFACE).GetInput('diffuseColor').Set(Gf.Vec3f(*rgb))
    return edit


def move(xyz):
    def edit(stage):
        from pxr import Gf, UsdGeom
        UsdGeom.XformCommonAPI(stage.GetPrimAtPath(SPHERE)).SetTranslate(Gf.Vec3d(*xyz))
    return edit


def expect_depth():
    """The depth increases from the center of the sphere, the closest point to the camera, to its sides. The depth is
    either Z or normalized, depending on HYDRA_NORMALIZE_DEPTH, so only the order is checked."""
    side = ScreenPosition((0.9, 0, (1 - 0.9 * 0.9) ** 0.5))

    def check(image):
        center_depth = image.Average(0.5, 0.5)[0]
        side_depth = image.Average(*side)[0]
        if 0 < center_depth < side_depth:
            return ''
        return 'expected 0 < depth at the center < depth on the side of the sphere, got {:.4f} and {:.4f}'.format(
            center_depth, side_depth)
    return check


scenario = Scenario(create_stage, [
    Step('initial render', check=ExpectColor(Color.RED)),
    Step('material color changed', edit=set_color((0.05, 0.8, 0.05)), check=ExpectColor(Color.GREEN)),
    # The color change starts the render, the move is done while it renders
    Step('moved out of the frame during the render', edit=set_color((0.05, 0.05, 0.8)), during_render=move((0, 10, 0)),
         check=ExpectColor(Color.EMPTY)),
    Step('moved back', edit=move((0, 0, 0)), check=ExpectColor(Color.BLUE)),
    Step('depth AOV', aov='depth', check=expect_depth()),
    Step('color AOV again', aov='color', check=ExpectColor(Color.BLUE)),
])

sys.exit(Run(scenario))
