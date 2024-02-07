# Copyright 2022 Autodesk, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""
Read an ass file and sort the nodes by name in alphabetical order.
Print the sorted nodes in the terminal or overwrite the original ass file
"""
from enum import Enum
from copy import copy
import argparse

parser = argparse.ArgumentParser(
            prog='check_new_hydra_tests',
            description='Compares the hydra test groups with the successful tests from the testsuite',)
parser.add_argument('assfile')
parser.add_argument('-w', '--overwrite')
args = parser.parse_args()

ASS_FILE = args.assfile 



class ParserState(Enum):
    LOOKING_FOR_NODE=1
    READING_NODE=2


parser_state = ParserState.LOOKING_FOR_NODE

NODENAMES = {
"abs",
"add",
"alembic",
"ambient_occlusion",
"aov_read_float",
"aov_read_int",
"aov_read_rgb",
"aov_read_rgba",
"aov_write_float",
"aov_write_int",
"aov_write_rgb",
"aov_write_rgba",
"aov_write_vector",
"atan",
"atmosphere_volume",
"barndoor",
"blackbody",
"blackman_harris_filter",
"box",
"box_filter",
"bump2d",
"bump3d",
"c4d_texture_tag",
"c4d_texture_tag_rgba",
"cache",
"camera_projection",
"car_paint",
"catrom_filter",
"cell_noise",
"checkerboard",
"clamp",
"clip_geo",
"closest_filter",
"collection",
"color_convert",
"color_correct",
"color_jitter",
"color_manager_ocio",
"compare",
"complement",
"complex_ior",
"composite",
"cone",
"contour_filter",
"cross",
"cryptomatte",
"cryptomatte_filter",
"cryptomatte_manifest_driver",
"curvature",
"curves",
"cyl_camera",
"cylinder",
"cylinder_light",
"diff_filter",
"disable",
"disk",
"disk_light",
"distance",
"distant_light",
"divide",
"dot",
"driver_deepexr",
"driver_exr",
"driver_jpeg",
"driver_no_op",
"driver_png",
"driver_tiff",
"exp",
"facing_ratio",
"farthest_filter",
"fisheye_camera",
"flakes",
"flat",
"float_to_int",
"float_to_matrix",
"float_to_rgb",
"float_to_rgba",
"fog",
"fraction",
"gaussian_filter",
"ginstance",
"gobo",
"hair",
"heatmap_filter",
"image",
"imager_color_correct",
"imager_color_curves",
"imager_denoiser_noice",
"imager_denoiser_oidn",
"imager_denoiser_optix",
"imager_exposure",
"imager_lens_effects",
"imager_light_mixer",
"imager_tonemap",
"imager_white_balance",
"implicit",
"include_graph",
"instancer",
"is_finite",
"lambert",
"layer_float",
"layer_rgba",
"layer_shader",
"length",
"light_blocker",
"light_decay",
"log",
"material",
"materialx",
"matrix_interpolate",
"matrix_multiply_vector",
"matrix_transform",
"matte",
"max",
"maya_layered_shader",
"merge",
"mesh_light",
"min",
"mitnet_filter",
"mix_rgba",
"mix_shader",
"modulo",
"motion_vector",
"multiply",
"negate",
"noise",
"normal_map",
"normalize",
"nurbs",
"options",
"ortho_camera",
"osl",
"override",
"passthrough",
"persp_camera",
"photometric_light",
"physical_sky",
"plane",
"point_light",
"points",
"polymesh",
"pow",
"procedural",
"quad_light",
"query_shape",
"ramp_float",
"ramp_rgb",
"random",
"range",
"ray_switch_rgba",
"ray_switch_shader",
"reciprocal",
"rgb_to_float",
"rgb_to_vector",
"rgba_to_float",
"round_corners",
"set_parameter",
"set_transform",
"shadow_matte",
"shuffle",
"sign",
"sinc_filter",
"skin",
"sky",
"skydome_light",
"space_transform",
"sphere",
"spherical_camera",
"spot_light",
"sqrt",
"standard",
"standard_hair",
"standard_surface",
"standard_volume",
"state_float",
"state_int",
"state_vector",
"string_replace",
"subtract",
"switch_operator",
"switch_rgba",
"switch_shader",
"thin_film",
"toon",
"trace_set",
"triangle_filter",
"trigo",
"triplanar",
"two_sided",
"usd",
"user_data_float",
"user_data_int",
"user_data_rgb",
"user_data_rgba",
"user_data_string",
"utility",
"uv_camera",
"uv_projection",
"uv_transform",
"variance_filter",
"vector_map",
"vector_to_rgb",
"visible_light",
"volume",
"volume_collector",
"volume_implicit",
"volume_sample_float",
"volume_sample_rgb",
"vr_camera",
"wireframe",
}

nodes = {}

with open(ASS_FILE) as f:
    for line in f:
        line = line.rstrip()
        if parser_state == ParserState.LOOKING_FOR_NODE:
            if line in NODENAMES:
                parser_state = ParserState.READING_NODE
                current_node = []
                current_node_name = ""
                current_node.append(line)

        elif parser_state == ParserState.READING_NODE:
            current_node.append(line)
            if line.startswith(" name "):
                current_node_name = line[6:]
            elif line == "}":
                nodes[current_node_name] = copy(current_node)
                parser_state = ParserState.LOOKING_FOR_NODE
if args.overwrite: 
    with open(ASS_FILE, 'w') as f:
        for node_name, node_content in sorted(nodes.items()):
            for line in node_content:
                f.write(line+'\n')
            f.write("\n")
else:
    for node_name, node_content in sorted(nodes.items()):
        for line in node_content:
            print(line)
        print("")
