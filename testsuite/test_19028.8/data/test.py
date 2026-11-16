# Purpose changes: the purposes shown by the engine change between the renders, see the purposes of Step in
# interactive_engine_test.py
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    AllOf, BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor, Run, Scenario,
    ScreenPosition, SetTranslate, Step)

left = ScreenPosition((-1.8, 0, 0))
right = ScreenPosition((1.8, 0, 0))


def create_stage():
    """A blue proxy cube on the left, a red render sphere in the center and a green guide sphere on the right"""
    from pxr import Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, '/World/camera', (0, 0, 6))
    DefineDistantLight(stage, '/World/light')
    prims = (
        (UsdGeom.Cube, '/World/proxy', -1.8, UsdGeom.Tokens.proxy, (0.05, 0.05, 0.8)),
        (UsdGeom.Sphere, '/World/render', 0.0, UsdGeom.Tokens.render, (0.8, 0.05, 0.05)),
        (UsdGeom.Sphere, '/World/guide', 1.8, UsdGeom.Tokens.guide, (0.05, 0.8, 0.05)),
    )
    for schema, path, x, purpose, color in prims:
        prim = schema.Define(stage, path)
        prim.CreatePurposeAttr().Set(purpose)
        SetTranslate(stage, path, (x, 0, 0))
        BindMaterial(stage, path, DefinePreviewMaterial(stage, path.replace('/World/', '/World/materials/'), color))
    return stage


def expect(proxy, render, guide):
    return AllOf(ExpectColor(Color.BLUE if proxy else Color.EMPTY, left),
                 ExpectColor(Color.RED if render else Color.EMPTY),
                 ExpectColor(Color.GREEN if guide else Color.EMPTY, right))


scenario = Scenario(create_stage, [
    # The default render params of the engine only show the proxy purpose
    Step('initial render, proxy shown', check=expect(True, False, False)),
    Step('render shown', purposes=('render',), check=expect(False, True, False)),
    Step('all the purposes shown', purposes=('proxy', 'render', 'guide'), check=expect(True, True, True)),
    Step('guide shown', purposes=('guide',), check=expect(False, False, True)),
    Step('proxy and render shown', purposes=('proxy', 'render'), check=expect(True, True, False)),
])

sys.exit(Run(scenario))
