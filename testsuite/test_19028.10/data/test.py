# Interactive edits of a shader imager graph, set by the arnold:global:imager render setting: parameter edits, before
# and during the render, an imager inserted in the chain and removed, the root imager replaced by an imager_shader whose
# shader is edited during the render, and the imager setting cleared. Covers the fixes of the imager updates (#2758,
# #2764): imager edits used the request_imager_update hint instead of AiImagerInterrupt, imagers resumed before the
# connections were processed, and removed imager nodes were destroyed during the render. See
# interactive_engine_test.py.
import os
import sys

sys.path.insert(0, os.environ['ARNOLD_TESTSUITE_COMMON'])
from interactive_engine_test import (  # noqa: E402
    BindMaterial, Color, DefineCamera, DefineDistantLight, DefinePreviewMaterial, ExpectColor, Run, Scenario,
    Step)

SPHERE = '/World/sphere'
LIGHT = '/World/light'
IMAGER = '/World/imager'
EXPOSURE = IMAGER + '/exposure'
COLOR_CORRECT = IMAGER + '/color_correct'
SHADER_IMAGER = IMAGER + '/shader_imager'
FLAT = IMAGER + '/flat'


def create_stage():
    """A red sphere, and an ArnoldNodeGraph whose root imager is an imager_exposure of 0"""
    from pxr import Sdf, Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(stage, '/World')
    DefineCamera(stage, '/World/camera', (0, 0, 6))
    DefineDistantLight(stage, LIGHT)
    UsdGeom.Sphere.Define(stage, SPHERE).CreateRadiusAttr().Set(1.0)
    BindMaterial(stage, SPHERE, DefinePreviewMaterial(stage, '/World/materials/red', (0.8, 0.05, 0.05)))
    stage.DefinePrim(IMAGER, 'ArnoldNodeGraph')
    exposure = define_imager(stage, EXPOSURE, 'arnold:imager_exposure')
    exposure.CreateInput('exposure', Sdf.ValueTypeNames.Float).Set(0.0)
    set_root(stage, exposure)
    return stage


def define_imager(stage, path, id):
    from pxr import Sdf, UsdShade
    shader = UsdShade.Shader.Define(stage, path)
    shader.CreateIdAttr(id)
    shader.CreateOutput('shader', Sdf.ValueTypeNames.Token)
    return shader


def set_root(stage, shader):
    """Connects the output of the imager graph, i.e. its root imager"""
    from pxr import Sdf
    output = stage.GetPrimAtPath(IMAGER).CreateAttribute('outputs:input', Sdf.ValueTypeNames.Token)
    output.SetConnections([Sdf.Path(shader.GetPath()).AppendProperty('outputs:shader')])


def set_exposure(value):
    def edit(stage):
        from pxr import UsdShade
        UsdShade.Shader.Get(stage, EXPOSURE).GetInput('exposure').Set(value)
    return edit


def set_light_intensity(value):
    """An edit restarting the render without changing the colors checked, for the edits during the render"""
    def edit(stage):
        from pxr import UsdLux
        UsdLux.DistantLight(stage.GetPrimAtPath(LIGHT)).GetIntensityAttr().Set(value)
    return edit


def insert_color_correct(stage):
    """Inserts an imager_color_correct after the exposure one, making the image green"""
    from pxr import Gf, Sdf, UsdShade
    color_correct = define_imager(stage, COLOR_CORRECT, 'arnold:imager_color_correct')
    color_correct.CreateInput('main_gain', Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0.05, 1, 0.05))
    color_correct.CreateInput('main_offset', Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0, 0.3, 0))
    UsdShade.Shader.Get(stage, EXPOSURE).CreateInput('input', Sdf.ValueTypeNames.Token).ConnectToSource(
        color_correct.GetOutput('shader'))


def remove_color_correct(stage):
    from pxr import UsdShade
    UsdShade.Shader.Get(stage, EXPOSURE).GetInput('input').GetAttr().ClearConnections()
    stage.RemovePrim(COLOR_CORRECT)


def replace_by_shader_imager(stage):
    """Replaces the root imager by an imager_shader rendering a blue flat shader"""
    from pxr import Gf, Sdf, UsdShade
    flat = UsdShade.Shader.Define(stage, FLAT)
    flat.CreateIdAttr('arnold:flat')
    flat.CreateInput('color', Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0.05, 0.05, 0.8))
    flat.CreateOutput('out', Sdf.ValueTypeNames.Color3f)
    shader_imager = define_imager(stage, SHADER_IMAGER, 'arnold:imager_shader')
    shader_imager.CreateInput('shader', Sdf.ValueTypeNames.Token).ConnectToSource(flat.GetOutput('out'))
    set_root(stage, shader_imager)
    stage.RemovePrim(EXPOSURE)


def set_flat_color(stage):
    from pxr import Gf, UsdShade
    UsdShade.Shader.Get(stage, FLAT).GetInput('color').Set(Gf.Vec3f(0.05, 0.8, 0.05))



scenario = Scenario(create_stage, [
    Step('initial render, with an exposure imager of 0', render_settings={'arnold:global:imager': IMAGER},
         check=ExpectColor(Color.RED)),
    Step('exposure of -10', edit=set_exposure(-10.0), check=ExpectColor(Color.BLACK)),
    Step('exposure back to 0 during the render', edit=set_light_intensity(2.5), during_render=set_exposure(0.0),
         check=ExpectColor(Color.RED)),
    Step('imager_color_correct inserted after the exposure imager', edit=insert_color_correct,
         check=ExpectColor(Color.GREEN)),
    Step('imager_color_correct removed', edit=remove_color_correct, check=ExpectColor(Color.RED)),
    Step('root imager replaced by an imager_shader of a blue flat shader', edit=replace_by_shader_imager,
         check=ExpectColor(Color.BLUE)),
    Step('flat shader of the imager_shader made green during the render', edit=set_light_intensity(3.0),
         during_render=set_flat_color, check=ExpectColor(Color.GREEN)),
    Step('imager setting cleared', render_settings={'arnold:global:imager': ''}, check=ExpectColor(Color.RED)),
])

sys.exit(Run(scenario))
