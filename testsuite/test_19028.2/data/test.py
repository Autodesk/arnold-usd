# The scenario of test_2343.8 (data/test.cpp), with the render delegate only: its skipped mode is a procedural one.
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    AllOf, BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor, Run, Scenario,
    ScreenPosition, SetTranslate, Step)

LEFT = '/World/left'
RIGHT = '/World/right'
CENTER = '/World/center'
RED_LIGHT = '/World/red_light'
GREEN_LIGHT = '/World/green_light'
BLUE_LIGHT = '/World/blue_light'

left = ScreenPosition((-1.8, 0, 0))
right = ScreenPosition((1.8, 0, 0))
center = ScreenPosition((0, 0, 0))


def light_link(stage, light):
    from pxr import UsdLux
    return UsdLux.LightAPI(stage.GetPrimAtPath(light)).GetLightLinkCollectionAPI()


def set_links(stage, light, prims, include):
    """Links the light to the given prims only, or to everything but them"""
    collection = light_link(stage, light)
    collection.CreateIncludeRootAttr().Set(not include)
    collection.CreateIncludesRel().SetTargets(prims if include else [])
    collection.CreateExcludesRel().SetTargets([] if include else prims)


def define_colored_light(stage, path, color):
    from pxr import Gf
    DefineDistantLight(stage, path).CreateColorAttr().Set(Gf.Vec3f(*color))


def define_sphere(stage, path, position):
    from pxr import UsdGeom, UsdShade
    UsdGeom.Sphere.Define(stage, path).CreateRadiusAttr().Set(0.8)
    SetTranslate(stage, path, position)
    BindMaterial(stage, path, UsdShade.Material.Get(stage, '/World/materials/grey'))


def create_stage():
    from pxr import Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, '/World/camera', (0, 0, 6))
    DefinePreviewMaterial(stage, '/World/materials/grey', (0.5, 0.5, 0.5))
    define_sphere(stage, LEFT, (-1.8, 0, 0))
    define_sphere(stage, RIGHT, (1.8, 0, 0))
    # The red light only lights the left sphere, and the green one only the right sphere
    define_colored_light(stage, RED_LIGHT, (1, 0, 0))
    define_colored_light(stage, GREEN_LIGHT, (0, 1, 0))
    set_links(stage, RED_LIGHT, [RIGHT], False)
    set_links(stage, GREEN_LIGHT, [LEFT], False)
    return stage


def swap_links(stage):
    set_links(stage, RED_LIGHT, [RIGHT], True)
    set_links(stage, GREEN_LIGHT, [LEFT], True)


def add_blue_light(stage):
    from pxr import UsdLux
    define_colored_light(stage, BLUE_LIGHT, (0, 0, 1))
    set_links(stage, BLUE_LIGHT, [LEFT], True)
    UsdLux.DistantLight.Get(stage, GREEN_LIGHT).GetIntensityAttr().Set(0.0)


def remove_blue_light_linking(stage):
    collection = light_link(stage, BLUE_LIGHT)
    collection.GetIncludeRootAttr().Clear()
    collection.GetIncludesRel().ClearTargets(True)


scenario = Scenario(create_stage, [
    Step('initial render', check=AllOf(ExpectColor(Color.RED, left), ExpectColor(Color.GREEN, right))),
    Step('light link excludes change, the red light lighting none of the spheres',
         edit=lambda stage: set_links(stage, RED_LIGHT, [LEFT, RIGHT], False),
         check=AllOf(ExpectColor(Color.BLACK, left), ExpectColor(Color.GREEN, right))),
    Step('light link includes, swapping the spheres lit by each light', edit=swap_links,
         check=AllOf(ExpectColor(Color.GREEN, left), ExpectColor(Color.RED, right))),
    Step('new blue light only lighting the left sphere, and the green light turned off', edit=add_blue_light,
         check=AllOf(ExpectColor(Color.BLUE, left), ExpectColor(Color.RED, right))),
    Step('the red light, only lighting the right sphere, is removed', edit=lambda stage: stage.RemovePrim(RED_LIGHT),
         check=AllOf(ExpectColor(Color.BLUE, left), ExpectColor(Color.BLACK, right))),
    Step('new sphere in the center, that no light is linked to',
         edit=lambda stage: define_sphere(stage, CENTER, (0, 0, 0)),
         check=AllOf(ExpectColor(Color.BLUE, left), ExpectColor(Color.BLACK, center), ExpectColor(Color.BLACK, right))),
    Step('the new sphere is added to the blue light includes',
         edit=lambda stage: set_links(stage, BLUE_LIGHT, [LEFT, CENTER], True),
         check=AllOf(ExpectColor(Color.BLUE, left), ExpectColor(Color.BLUE, center), ExpectColor(Color.BLACK, right))),
    Step('light linking removed from the blue light, which lights everything', edit=remove_blue_light_linking,
         check=AllOf(ExpectColor(Color.BLUE, left), ExpectColor(Color.BLUE, center), ExpectColor(Color.BLUE, right))),
])

sys.exit(Run(scenario))
