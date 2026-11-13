# The scenario of test_2343.1 (data/test.cpp), with the render delegate only: the known issues of that test are in the
# procedural modes.
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor, Run, Scenario,
    SetTranslate, Step)

SPHERE = '/World/sphere'
CUBE = '/World/cube'


def create_stage():
    from pxr import Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, '/World/camera', (0, 0, 6))
    DefineDistantLight(stage, '/World/light')
    UsdGeom.Sphere.Define(stage, SPHERE).CreateRadiusAttr().Set(1.0)
    SetTranslate(stage, SPHERE, (0, 0, 0))
    BindMaterial(stage, SPHERE, DefinePreviewMaterial(stage, '/World/materials/sphere_mat', (0.8, 0.05, 0.05)))
    return stage


def change_material(stage):
    from pxr import Gf, UsdShade
    UsdShade.Shader.Get(stage, '/World/materials/sphere_mat/surface').GetInput('diffuseColor').Set(
        Gf.Vec3f(0.05, 0.8, 0.05))


def add_cube(stage):
    from pxr import UsdGeom
    UsdGeom.Cube.Define(stage, CUBE).CreateSizeAttr().Set(1.0)
    BindMaterial(stage, CUBE, DefinePreviewMaterial(stage, '/World/materials/cube_mat', (0.05, 0.05, 0.8)))


scenario = Scenario(create_stage, [
    Step('initial render', check=ExpectColor(Color.RED)),
    Step('material attribute change', edit=change_material, check=ExpectColor(Color.GREEN)),
    Step('transform change, moving the sphere out of the frame',
         edit=lambda stage: SetTranslate(stage, SPHERE, (0, 10, 0)), check=ExpectColor(Color.EMPTY)),
    Step('new prim with a new material', edit=add_cube, check=ExpectColor(Color.BLUE)),
    Step('prim removal', edit=lambda stage: stage.RemovePrim(CUBE), check=ExpectColor(Color.EMPTY)),
    Step('transform change, moving the sphere back', edit=lambda stage: SetTranslate(stage, SPHERE, (0, 0, 0)),
         check=ExpectColor(Color.GREEN)),
])

sys.exit(Run(scenario))
