# Layer edits: a sublayer is added, muted and unmuted, a payload is loaded and unloaded, and a variant is switched and
# edited through a variant edit target
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    AllOf, BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor, Run, Scenario,
    ScreenPosition, SetTranslate, Step)

SPHERE = '/World/sphere'
PAYLOAD = '/World/payload'
left = ScreenPosition((-1.8, 0, 0))
right = ScreenPosition((1.8, 0, 0))
above = ScreenPosition((0, 1.5, 0))

# The anonymous layers are kept alive by these references, not only by the stage, as they're muted or unloaded
layers = {}


def create_payload_layer():
    """A layer whose default prim holds a green cube on the right"""
    from pxr import Sdf, Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    root = UsdGeom.Xform.Define(stage, '/root')
    UsdGeom.Cube.Define(stage, '/root/cube').CreateSizeAttr().Set(1.2)
    SetTranslate(stage, '/root/cube', (1.8, 0, 0))
    BindMaterial(stage, '/root/cube', DefinePreviewMaterial(stage, '/root/material', (0.05, 0.8, 0.05)))
    stage.SetDefaultPrim(root.GetPrim())
    layer = Sdf.Layer.CreateAnonymous('payload')
    layer.TransferContent(stage.GetRootLayer())
    return layer


def create_stage():
    """A sphere in the center, whose 'color' variant set binds a red or a green material, and an unloaded payload"""
    from pxr import Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory(load=Usd.Stage.LoadNone)
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, '/World/camera', (0, 0, 6))
    DefineDistantLight(stage, '/World/light')
    red = DefinePreviewMaterial(stage, '/World/materials/red', (0.8, 0.05, 0.05))
    green = DefinePreviewMaterial(stage, '/World/materials/green', (0.05, 0.8, 0.05))
    sphere = UsdGeom.Sphere.Define(stage, SPHERE)
    sphere.CreateRadiusAttr().Set(1.0)
    variants = sphere.GetPrim().GetVariantSets().AddVariantSet('color')
    for name, material in (('red', red), ('green', green)):
        variants.AddVariant(name)
        variants.SetVariantSelection(name)
        with variants.GetVariantEditContext():
            BindMaterial(stage, SPHERE, material)
    variants.SetVariantSelection('red')

    layers['payload'] = create_payload_layer()
    stage.DefinePrim(PAYLOAD).GetPayloads().AddPayload(layers['payload'].identifier)
    return stage


def add_sublayer(stage):
    """A sublayer defining a blue sphere on the left"""
    from pxr import Sdf, Usd, UsdGeom
    sublayer_stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(sublayer_stage, '/World')
    UsdGeom.Sphere.Define(sublayer_stage, '/World/sublayer_sphere').CreateRadiusAttr().Set(0.8)
    SetTranslate(sublayer_stage, '/World/sublayer_sphere', (-1.8, 0, 0))
    BindMaterial(sublayer_stage, '/World/sublayer_sphere',
                 DefinePreviewMaterial(sublayer_stage, '/World/materials/blue', (0.05, 0.05, 0.8)))
    layers['sublayer'] = Sdf.Layer.CreateAnonymous('sublayer')
    layers['sublayer'].TransferContent(sublayer_stage.GetRootLayer())
    stage.GetRootLayer().subLayerPaths.append(layers['sublayer'].identifier)


def select_variant(name):
    return lambda stage: stage.GetPrimAtPath(SPHERE).GetVariantSets().GetVariantSet('color').SetVariantSelection(name)


def move_in_green_variant(stage):
    """Moves the sphere up in the green variant only, through a variant edit target"""
    variants = stage.GetPrimAtPath(SPHERE).GetVariantSets().GetVariantSet('color')
    with variants.GetVariantEditContext():
        SetTranslate(stage, SPHERE, (0, 1.5, 0))


def expect(left_color, center_color, right_color):
    return AllOf(ExpectColor(left_color, left), ExpectColor(center_color), ExpectColor(right_color, right))


scenario = Scenario(create_stage, [
    Step('initial render, the payload is unloaded', check=expect(Color.EMPTY, Color.RED, Color.EMPTY)),
    Step('sublayer added', edit=add_sublayer, check=expect(Color.BLUE, Color.RED, Color.EMPTY)),
    Step('sublayer muted', edit=lambda stage: stage.MuteLayer(layers['sublayer'].identifier),
         check=expect(Color.EMPTY, Color.RED, Color.EMPTY)),
    Step('sublayer unmuted', edit=lambda stage: stage.UnmuteLayer(layers['sublayer'].identifier),
         check=expect(Color.BLUE, Color.RED, Color.EMPTY)),
    Step('payload loaded', edit=lambda stage: stage.Load(PAYLOAD), check=expect(Color.BLUE, Color.RED, Color.GREEN)),
    Step('payload unloaded', edit=lambda stage: stage.Unload(PAYLOAD),
         check=expect(Color.BLUE, Color.RED, Color.EMPTY)),
    Step('variant switched to green', edit=select_variant('green'), check=expect(Color.BLUE, Color.GREEN, Color.EMPTY)),
    Step('sphere moved up in the green variant, through a variant edit target', edit=move_in_green_variant,
         check=AllOf(expect(Color.BLUE, Color.EMPTY, Color.EMPTY), ExpectColor(Color.GREEN, above))),
    Step('variant switched back to red, which isn\'t moved', edit=select_variant('red'),
         check=AllOf(expect(Color.BLUE, Color.RED, Color.EMPTY), ExpectColor(Color.EMPTY, above))),
])

sys.exit(Run(scenario))
