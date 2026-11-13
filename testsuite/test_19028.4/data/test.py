# Edits while the render is paused or stopped, see the actions of Step in interactive_engine_test.py
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    AllOf, BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor, PAUSE_RESUME,
    Run, Scenario, ScreenPosition, SetTranslate, STOP_RESTART, Step)

SPHERE = '/World/sphere'
right = ScreenPosition((1.5, 0, 0))


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


def set_color(stage, rgb):
    from pxr import Gf, UsdShade
    UsdShade.Shader.Get(stage, '/World/materials/sphere_mat/surface').GetInput('diffuseColor').Set(Gf.Vec3f(*rgb))


def move_back_blue(stage):
    SetTranslate(stage, SPHERE, (0, 0, 0))
    set_color(stage, (0.05, 0.05, 0.8))


scenario = Scenario(create_stage, [
    Step('initial render', check=ExpectColor(Color.RED)),
    Step('material attribute change while paused', action=PAUSE_RESUME,
         edit=lambda stage: set_color(stage, (0.05, 0.8, 0.05)), check=ExpectColor(Color.GREEN)),
    Step('transform change while stopped', action=STOP_RESTART,
         edit=lambda stage: SetTranslate(stage, SPHERE, (1.5, 0, 0)),
         check=AllOf(ExpectColor(Color.GREEN, right), ExpectColor(Color.EMPTY))),
    Step('paused and resumed without edit', action=PAUSE_RESUME,
         check=AllOf(ExpectColor(Color.GREEN, right), ExpectColor(Color.EMPTY))),
    Step('stopped and restarted without edit', action=STOP_RESTART,
         check=AllOf(ExpectColor(Color.GREEN, right), ExpectColor(Color.EMPTY))),
    Step('transform and material changes while paused', action=PAUSE_RESUME, edit=move_back_blue,
         check=ExpectColor(Color.BLUE)),
], aa_samples=8)

sys.exit(Run(scenario))
