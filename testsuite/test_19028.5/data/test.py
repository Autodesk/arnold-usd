# AOV switches: the renderer AOV of the engine is changed between the renders, see the aov of Step in
# interactive_engine_test.py
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    ANY_DELEGATE, AllOf, BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor,
    KnownIssue, Run, Scenario, ScreenPosition, SetTranslate, Step)

LEFT = '/World/left'
RIGHT = '/World/right'
left = ScreenPosition((-1.5, 0, 0))
right = ScreenPosition((1.5, 0, 0))
# Where nothing is rendered
background = (0.5, 0.1)


def create_stage():
    """A red sphere on the left, and a green 2x2 quad facing the camera on the right, with an st primvar going from
    (0, 0) at its bottom left corner to (1, 1) at its top right one"""
    from pxr import Gf, Sdf, Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, '/World/camera', (0, 0, 6))
    DefineDistantLight(stage, '/World/light')
    UsdGeom.Sphere.Define(stage, LEFT).CreateRadiusAttr().Set(1.0)
    SetTranslate(stage, LEFT, (-1.5, 0, 0))
    BindMaterial(stage, LEFT, DefinePreviewMaterial(stage, '/World/materials/left', (0.8, 0.05, 0.05)))
    quad = UsdGeom.Mesh.Define(stage, RIGHT)
    quad.CreatePointsAttr().Set([Gf.Vec3f(-1, -1, 0), Gf.Vec3f(1, -1, 0), Gf.Vec3f(1, 1, 0), Gf.Vec3f(-1, 1, 0)])
    quad.CreateFaceVertexCountsAttr().Set([4])
    quad.CreateFaceVertexIndicesAttr().Set([0, 1, 2, 3])
    UsdGeom.PrimvarsAPI(quad).CreatePrimvar('st', Sdf.ValueTypeNames.TexCoord2fArray, UsdGeom.Tokens.vertex).Set(
        [Gf.Vec2f(0, 0), Gf.Vec2f(1, 0), Gf.Vec2f(1, 1), Gf.Vec2f(0, 1)])
    SetTranslate(stage, RIGHT, (1.5, 0, 0))
    BindMaterial(stage, RIGHT, DefinePreviewMaterial(stage, '/World/materials/right', (0.05, 0.8, 0.05)))
    return stage


def expect_depth(image):
    """The depth increases from the center of a sphere, the closest point to the camera, to its sides. It's Z or
    normalized, depending on HYDRA_NORMALIZE_DEPTH, so only the order is checked."""
    center = image.Average(*left)[0]
    side = image.Average(*ScreenPosition((-1.5 + 0.9, 0, (1 - 0.9 * 0.9) ** 0.5)))[0]
    if 0 < center < side:
        return ''
    return 'expected 0 < depth at the center of the left sphere < depth on its side, got {:.4f} and {:.4f}'.format(
        center, side)


def expect_prim_ids(image):
    """Each prim has its own id, the background has none"""
    ids = [image.Average(*position, radius=0)[0] for position in (left, right, background)]
    if ids[0] >= 0 and ids[1] >= 0 and ids[0] != ids[1] and ids[2] < 0:
        return ''
    return 'expected 2 different ids >= 0 on the prims and < 0 on the background, got {}'.format(ids)


def expect_st(image):
    """The st of the quad, at its bottom left, center and top right"""
    errors = []
    for x, y, expected in ((-0.6, -0.6, (0.2, 0.2)), (0, 0, (0.5, 0.5)), (0.6, 0.6, (0.8, 0.8))):
        st = image.Average(*ScreenPosition((1.5 + x, y, 0)), radius=0)[:2]
        if any(abs(value - e) > 0.05 for value, e in zip(st, expected)):
            errors.append('expected an st of {} at ({}, {}) on the quad, got ({:.3f}, {:.3f})'.format(
                expected, x, y, *st))
    return '; '.join(errors)


color_check = AllOf(
    ExpectColor(Color.RED, left), ExpectColor(Color.GREEN, right), ExpectColor(Color.EMPTY, *background))

scenario = Scenario(create_stage, [
    Step('initial render', check=color_check),
    Step('depth AOV', aov='depth', check=expect_depth),
    Step('primId AOV', aov='primId', check=expect_prim_ids),
    Step('primvars:st AOV', aov='primvars:st', check=expect_st, known_issues=[KnownIssue(
        ANY_DELEGATE, 'the default AOV descriptor of the primvars:<name> AOVs has no sourceType=primvar setting, so '
        'the render pass asks Arnold for a raw AOV of that name, which renders nothing '
        '(HdArnoldRenderDelegate::GetDefaultAovDescriptor)')]),
    Step('color AOV again', aov='color', check=color_check),
])

sys.exit(Run(scenario))
