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

from build_tools import convert_usd_version_to_int

ARNOLD_CLASS_NAMES = [
    'Alembic', 'Box', 'Cone', 'Curves', 'Disk', 'Implicit', 'Nurbs', 'Plane',
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
    import system
    usd_version = convert_usd_version_to_int(env['USD_VERSION'])
    configure(source, target, env, {
        'LIB_EXTENSION': system.LIB_EXTENSION,
        'RENDERER_PLUGIN_BASE': 'HdRendererPlugin' if usd_version >= 1910 else 'HdxRendererPlugin'
    })

def configure_usd_maging_plug_info(source, target, env):
    import system
    usd_version = convert_usd_version_to_int(env['USD_VERSION'])
    register_arnold_types = '\n'.join(['"UsdImagingArnold{}Adapter":{{"bases":["UsdImagingGprimAdapter"],"primTypeName":"Arnold{}"}},'.format(name, name) for name in ARNOLD_CLASS_NAMES])
    configure(source, target, env, {
        'LIB_EXTENSION': system.LIB_EXTENSION,
        'RENDERER_PLUGIN_BASE': 'HdRendererPlugin' if usd_version >= 1910 else 'HdxRendererPlugin',
        'REGISTER_ARNOLD_TYPES': register_arnold_types,
    })

def configure_header_file(source, target, env):
    usd_version = env['USD_VERSION'].split('.')
    arnold_version = env['ARNOLD_VERSION'].split('.')
    configure(source, target, env, {
        'USD_MAJOR_VERSION': usd_version[0],
        'USD_MINOR_VERSION': usd_version[1],
        'USD_PATCH_VERSION': usd_version[2],
        'ARNOLD_VERSION_ARCH_NUM': arnold_version[0],
        'ARNOLD_VERSION_MAJOR_NUM': arnold_version[1],
        'ARNOLD_VERSION_MINOR_NUM': arnold_version[2],
    })

def configure_shape_adapters(source, target, env):
    create_adapter_classes = '\n'.join(['CREATE_ADAPTER_CLASS({});'.format(name) for name in ARNOLD_CLASS_NAMES])
    register_adapter_classes = '\n'.join(['REGISTER_ADAPTER_CLASS({});'.format(name) for name in ARNOLD_CLASS_NAMES])
    configure(source, target, env, {
        'CREATE_ADAPTER_CLASSES': create_adapter_classes,
        'REGISTER_ADAPTER_CLASSES': register_adapter_classes,
    })
