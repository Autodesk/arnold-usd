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
from string import Template

from .build_tools import convert_usd_version_to_int
from . import system

ARNOLD_CLASS_NAMES = [
    'Alembic', 'Box', 'Cone', 'Curves', 'Disk', 'Ginstance', 'Implicit', 'Nurbs', 'Plane',
    'Points', 'Polymesh', 'Procedural', 'Sphere', 'Usd', 'Volume', 'VolumeImplicit']

class DotTemplate(Template):
    delimiter = '$'
    idpattern = r'[_a-z][_a-z0-9\.]*'

def configure(source, target, env, config):
    with open(source[0].get_abspath(), 'r') as src:
        src_contents = src.read()
        with open(target[0].get_abspath(), 'w') as trg:
            template = DotTemplate(src_contents)
            trg.write(template.substitute(config))

def configure_plug_info(source, target, env):
    usd_version = convert_usd_version_to_int(env['USD_VERSION'])
    configure(source, target, env, {
        'LIB_PATH': '../hdArnold',
        'LIB_EXTENSION': system.LIB_EXTENSION,
        'RENDERER_PLUGIN_BASE': 'HdRendererPlugin' if usd_version >= 1910 else 'HdxRendererPlugin',
    })

def configure_usd_imaging_plug_info(source, target, env):
    usd_version = convert_usd_version_to_int(env['USD_VERSION'])
    register_arnold_types = '\n'.join(['"UsdImagingArnold{}Adapter":{{"bases":["UsdImagingGprimAdapter"],"primTypeName":"Arnold{}"}},'.format(name, name) for name in ARNOLD_CLASS_NAMES])
    configure(source, target, env, {
        'LIB_PATH': '../usdImagingArnold',
        'LIB_EXTENSION': system.LIB_EXTENSION,
        'RENDERER_PLUGIN_BASE': 'HdRendererPlugin' if usd_version >= 1910 else 'HdxRendererPlugin',
        'REGISTER_ARNOLD_TYPES': register_arnold_types
    })

def configure_usd_imaging_proc_plug_info(source, target, env):
    usd_version = convert_usd_version_to_int(env['USD_VERSION'])
    register_arnold_types = '\n'.join(['"UsdImagingArnold{}Adapter":{{"bases":["UsdImagingGprimAdapter"],"primTypeName":"Arnold{}"}},'.format(name, name) for name in ARNOLD_CLASS_NAMES])
    configure(source, target, env, {
        'LIB_PATH': '../../usd_proc',
        'LIB_EXTENSION': system.LIB_EXTENSION,
        'RENDERER_PLUGIN_BASE': 'HdRendererPlugin' if usd_version >= 1910 else 'HdxRendererPlugin',
        'REGISTER_ARNOLD_TYPES': register_arnold_types
    })


def configure_ndr_plug_info(source, target, env):
    usd_version = convert_usd_version_to_int(env['USD_VERSION'])
    configure(source, target, env, {
        'LIB_PATH' : '../nodeRegistryArnold',
        'LIB_EXTENSION': system.LIB_EXTENSION,
        'REGISTRY_BASE': 'Ndr' if usd_version < 2505 else 'Sdr',
    })

def configure_procedural_ndr_plug_info(source, target, env):
    usd_version = convert_usd_version_to_int(env['USD_VERSION'])
    configure(source, target, env, {
        'LIB_PATH': '../../usd_proc',
        'LIB_EXTENSION': system.LIB_EXTENSION,
        'REGISTRY_BASE': 'Ndr' if usd_version < 2505 else 'Sdr',
    })

# 'si' stands for 'scene index'
def configure_procedural_si_plug_info(source, target, env):
    usd_version = convert_usd_version_to_int(env['USD_VERSION'])
    configure(source, target, env, {
        'LIB_PATH': '../../usd_proc',
        'LIB_EXTENSION': system.LIB_EXTENSION,
        'RENDERER_PLUGIN_BASE': 'HdRendererPlugin' if usd_version >= 1910 else 'HdxRendererPlugin',
    })

def configure_shape_adapters(source, target, env):
    create_adapter_classes = '\n'.join(['CREATE_ADAPTER_CLASS({});'.format(name) for name in ARNOLD_CLASS_NAMES])
    register_adapter_classes = '\n'.join(['REGISTER_ADAPTER_CLASS({});'.format(name) for name in ARNOLD_CLASS_NAMES])
    configure(source, target, env, {
        'CREATE_ADAPTER_CLASSES': create_adapter_classes,
        'REGISTER_ADAPTER_CLASSES': register_adapter_classes,
    })
