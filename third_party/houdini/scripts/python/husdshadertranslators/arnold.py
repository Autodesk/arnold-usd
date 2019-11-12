import hou
import husdshadertranslators.utils as utils
from husdshadertranslators.default import DefaultShaderTranslatorHelper, renderContextName, RampParmTranslator

from pxr import Usd, UsdShade, Sdf, Vt

from itertools import izip


# TODO(pal):
# - Support putting fetch nodes in the middle of the shader graph.
# - Investigate animated parameters, especially the ramp.
# - Filter the extra parameters created on the ramp nodes.

# Arnold shaders have the render mask of VMantra. This would be great to change
# as mantra and other shader types might share this mask.
ARNOLD_RENDER_MASK = 'VMantra'
ARNOLD_TERMINALS = [hou.shaderType.Surface, hou.shaderType.Displacement, 'volume']
ARNOLD_NODE_PREFIX = 'arnold::'
ARNOLD_USD_PREFIX = 'arnold:'
ARNOLD_FETCH_NAME = 'arnold::fetch'
ARNOLD_RENDER_CONTEXT_NAME = 'arnold'
ARNOLD_RAMP_TYPES = ['arnold::ramp_rgb', 'arnold::ramp_float']
ARNOLD_RAMP_INTERP_REMAP = {
    hou.rampBasis.Constant: 0,
    hou.rampBasis.Linear: 1,
    hou.rampBasis.CatmullRom: 2,
    hou.rampBasis.BSpline: 2,
    hou.rampBasis.MonotoneCubic: 3,
}

def resolve_fetch_vop(node):
    while node.type().name() == ARNOLD_FETCH_NAME:
        if node.isBypassed():
            return None
        node = node.parm('target').evalAsNode()
        if not node:
            return None
    return node

class ArnoldRampParmTranslator(RampParmTranslator):
    """
    Translator for Arnold ramp shader params.
    """

    def createAndSetAttrib(self, usd_shader, time_code):
        """ Creates an attribute on the usd shader primitive and sets its value
            according to the member node parameter, at the specified time code.
        """
        ramp = self.valueFromParm(self.parmTuple(), time_code)[0]
        output_time_code = self.adjustTimeCode(time_code)

        position = usd_shader.CreateInput('position', Sdf.ValueTypeNames.FloatArray)
        position.Set(Vt.FloatArray(ramp.keys()), output_time_code)

        interpolation = usd_shader.CreateInput('interpolation', Sdf.ValueTypeNames.IntArray)
        interpolation.Set([ARNOLD_RAMP_INTERP_REMAP.get(v, 3) for v in ramp.basis()], output_time_code)

        if ramp.isColor():
            value_input = usd_shader.CreateInput('color', Sdf.ValueTypeNames.Color3fArray)
            value = ramp.values()
        else:
            value_input = usd_shader.CreateInput('value', Sdf.ValueTypeNames.FloatArray)
            value = Vt.FloatArray(ramp.values())
        value_input.Set(value, output_time_code)

class ArnoldShaderHelper(DefaultShaderTranslatorHelper):
    # Just initializing the default shader translator helper.
    def __init__(self, translator_id, usd_stage, usd_material_path, usd_time_code):
        DefaultShaderTranslatorHelper.__init__(self, translator_id, usd_stage, usd_material_path, usd_time_code)

    def createShaderPrimID(self, shader_prim, shader_node):
        """
        Creates and sets the id parameter on the shader. We are querying the shader_node's type name and removing the
        arnold:: prefix, and setting the id to arnold:<shader_type>
        """
        type_name = shader_node.type().name()
        if type_name.startswith(ARNOLD_NODE_PREFIX):
            shader_name = type_name[len(ARNOLD_NODE_PREFIX):]
            shader_prim.SetShaderId(ARNOLD_USD_PREFIX + shader_name)
        # Falling back to the built-in function.
        else:
            DefaultShaderTranslatorHelper.createShaderPrimID(self, shader_prim, shader_node)

    def createShaderPrimAttributes(self, shader_prim, shader_node):
        """ Creates and sets the shader parameters on the usd shader
            based on the given shader node.
        """
        for parm_tuple in shader_node.parmTuples():
            parm_template = parm_tuple.parmTemplate()
            if isinstance(parm_template, hou.FolderParmTemplate) or isinstance(parm_template, hou.FolderSetParmTemplate):
                continue

            parm_translator = self.getParmTranslator(parm_tuple)
            if parm_translator is None:
                continue

            # Create an attribute on the usd prim and set its value.
            parm_translator.createAndSetAttrib(shader_prim, self.timeCode())

    def getRampParmTranslator(self, parm_tuple):
        """ Returns a translator for ramp parameters.
        """
        if parm_tuple.node().type().name() not in ARNOLD_RAMP_TYPES:
            return None
        return ArnoldRampParmTranslator(parm_tuple)

    def getRenderContextName(self, shader_node, shader_node_output_name):
        """ Returns the name of the render context to be used in material
            output name.
        """
        # We are only calling this helper on arnold shader, so we can just
        # hardcode the value.
        return ARNOLD_RENDER_CONTEXT_NAME

class ArnoldShaderTranslator(object):
    def __init__(self):
        self.my_id = -1

    def setTranslatorID(self, translator_id):
        self.my_id = translator_id

    def translatorID(self):
        return self.my_id

    def matchesRenderMask(self, render_mask):
        return render_mask == ARNOLD_RENDER_MASK

    def createMaterialShader(self, usd_stage, usd_material_path, usd_time_code, shader_node, shader_type, output_name):
        """ Creates a USD shader primitive that is part of the USD material
            Ie, the translator will connect the shader to the material output.

            usd_stage - UsdStage object on which to create the shader.
            usd_material_path - Path to the material primitive
                in which to create the shader.
            usd_time_code - time code (frame) at which to evaluate shader
                parameters and at which to set the primitive attributes.
            shader_node - Houdini node representing a shader.
            shader_type - Requested shader type to use, in case
                the shader node implements several shaders
                (eg, is a material builder).
            outupt_name - Particular output of the Houdini node that was
                used to arrive at the shader node and which represents the
                shader to translate (in case the node has several shaders).
                The output name can be an empty string, if node has no outputs.
        """
        shader_node = resolve_fetch_vop(shader_node)
        if shader_node is None:
            return
        type_name = shader_node.type().name()
        # If type name is 'arnold_material' then we are working with an output node, and we need to check the different
        # terminals. Otherwise we are just dealing with the first node.
        shaders_to_translate = []
        if type_name == 'arnold_material':
            shaders_to_translate = [(input, terminal) for input, terminal in izip(shader_node.inputs(), ARNOLD_TERMINALS)
                                    if input is not None]
        elif type_name.startswith(ARNOLD_NODE_PREFIX):
            shaders_to_translate = [(shader_node, ARNOLD_TERMINALS[0])]
        else:
            # If we are dealing with non-arnold materials, we are running the default shader translator.
            helper = DefaultShaderTranslatorHelper(self.translatorID(), usd_stage, usd_material_path, usd_time_code)
            helper.createMaterialShader(shader_node, shader_type, output_name)
            return

        for shader, shader_type in shaders_to_translate:
            shader = resolve_fetch_vop(shader)
            if shader and not shader.isBypassed():
                helper = ArnoldShaderHelper(self.translatorID(), usd_stage, usd_material_path, usd_time_code)
                helper.createMaterialShader(shader, shader_type, output_name)

arnold_translator = ArnoldShaderTranslator()

def usdShaderTranslator():
    return arnold_translator
