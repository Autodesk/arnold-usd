# Edits done while Arnold renders: each step's edit starts a render, its during_render edit is done once that render
# is partly done, see Step in interactive_engine_test.py
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    AllOf, BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor, Run, Scenario,
    ScreenPosition, SetTranslate, Step)

SPHERE = '/World/sphere'
CUBE = '/World/cube'
LIGHT = '/World/light'
right = ScreenPosition((1.5, 0, 0))
left = ScreenPosition((-1.5, 0, 0))


def create_stage():
    from pxr import Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, '/World/camera', (0, 0, 6))
    DefineDistantLight(stage, LIGHT)
    UsdGeom.Sphere.Define(stage, SPHERE).CreateRadiusAttr().Set(1.0)
    SetTranslate(stage, SPHERE, (0, 0, 0))
    BindMaterial(stage, SPHERE, DefinePreviewMaterial(stage, '/World/materials/sphere_mat', (0.8, 0.05, 0.05)))
    return stage


def set_color(rgb):
    def edit(stage):
        from pxr import Gf, UsdShade
        UsdShade.Shader.Get(stage, '/World/materials/sphere_mat/surface').GetInput('diffuseColor').Set(Gf.Vec3f(*rgb))
    return edit


def move(xyz):
    return lambda stage: SetTranslate(stage, SPHERE, xyz)


def add_cube(stage):
    from pxr import UsdGeom
    UsdGeom.Cube.Define(stage, CUBE).CreateSizeAttr().Set(1.0)
    SetTranslate(stage, CUBE, (-1.5, 0, 0))
    BindMaterial(stage, CUBE, DefinePreviewMaterial(stage, '/World/materials/cube_mat', (0.05, 0.05, 0.8)))


def set_light_intensity(intensity):
    def edit(stage):
        from pxr import UsdLux
        UsdLux.DistantLight.Get(stage, LIGHT).GetIntensityAttr().Set(intensity)
    return edit


RED, GREEN, BLUE = (0.8, 0.05, 0.05), (0.05, 0.8, 0.05), (0.05, 0.05, 0.8)

scenario = Scenario(create_stage, [
    Step('initial render', check=ExpectColor(Color.RED)),
    Step('transform change during the render', edit=set_color(GREEN), during_render=move((1.5, 0, 0)),
         check=AllOf(ExpectColor(Color.GREEN, right), ExpectColor(Color.EMPTY))),
    Step('new prim with a new material during the render', edit=move((0, 0, 0)), during_render=add_cube,
         check=AllOf(ExpectColor(Color.GREEN), ExpectColor(Color.BLUE, left))),
    Step('prim removal during the render', edit=set_color(RED), during_render=lambda stage: stage.RemovePrim(CUBE),
         check=AllOf(ExpectColor(Color.RED), ExpectColor(Color.EMPTY, left))),
    Step('material attribute change during the render', edit=set_light_intensity(2.5), during_render=set_color(BLUE),
         check=ExpectColor(Color.BLUE)),
], aa_samples=8)

sys.exit(Run(scenario))
