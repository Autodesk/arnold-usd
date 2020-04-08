# vim: filetype=python
# Copyright 2020 Autodesk, Inc.
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
import build_tools, system

def add_optional_libs(env, libs):
    if env['USD_HAS_PYTHON_SUPPORT']:
        return libs + [env['PYTHON_LIB_NAME'], env['BOOST_LIB_NAME'] % 'python']
    else:
        return libs

# Returns a list of usd dependencies and source files.
# This only works with monolithic and shared usd dependencies.
def render_delegate(env, sources):
    if (env['USD_BUILD_MODE'] == 'monolithic'):
        usd_deps = [
            env['USD_MONOLITHIC_LIBRARY'],
            'tbb',
        ]
        if (system.IS_LINUX):
            usd_deps = usd_deps + ['dl']
        return (sources, add_optional_libs(env, usd_deps))
    else:
        usd_libs = [
            'arch',
            'plug',
            'tf',
            'vt',
            'gf',
            'work',
            'hf',
            'hd',
            'hdx',
            'sdf',
            'usdImaging',
            'usdLux',
            'pxOsd',
        ]

        usd_deps = ['tbb']
        usd_libs, usd_sources = build_tools.link_usd_libraries(env, usd_libs)
        usd_deps = usd_deps + usd_libs
        source_files = sources + usd_sources
        if (system.IS_LINUX):
            usd_deps = usd_deps + ['dl']
        return (source_files, add_optional_libs(env, usd_deps))

# This only works with monolithic and shared usd dependencies.
def ndr_plugin(env, sources):
    if env['USD_BUILD_MODE'] == 'monolithic':
        usd_deps = [
            env['USD_MONOLITHIC_LIBRARY'],
            'tbb',
        ]
        return (sources, add_optional_libs(env, usd_deps))
    else:
        usd_libs = [
            'arch',
            'tf',
            'gf',
            'vt',
            'ndr',
            'sdr',
            'sdf',
            'usd',
        ]

        usd_deps = ['tbb']
        usd_libs, usd_sources = build_tools.link_usd_libraries(env, usd_libs)
        usd_deps = usd_deps + usd_libs
        source_files = sources + usd_sources
        return (source_files, add_optional_libs(env, usd_deps))

def translator(env, sources):
    if env['USD_BUILD_MODE'] == 'monolithic':
        usd_deps = [
            'usd_translator',
            env['USD_MONOLITHIC_LIBRARY'],
            'tbb',
        ]
        return (sources, add_optional_libs(env, usd_deps))
    elif env['USD_BUILD_MODE'] == 'static':
        # static builds rely on a monolithic static library
        if system.IS_WINDOWS:
            usd_deps = [
                '-WHOLEARCHIVE:libusd_m', 
                'tbb',
                'Ws2_32',
                'Dbghelp',
                'Shlwapi', 
                'advapi32' 
            ]
        else:
            usd_deps = [
                'libusd_m', 
                'tbb',
            ]

            if system.IS_LINUX:
                usd_deps = usd_deps + ['dl', 'pthread']
        return (sources, add_optional_libs(env, ['usd_translator'] + usd_deps))
    else:  # shared libs
        usd_libs = [
            'sdf',
            'tf',
            'usd',
            'ar',
            'usdGeom',
            'usdShade',
            'vt',
            'usdLux',
            'gf',
            'usdVol',
        ]

        usd_deps = ['tbb']

        usd_libs, usd_sources = build_tools.link_usd_libraries(env, usd_libs)
        source_files = sources + usd_sources
        return (source_files, add_optional_libs(env, ['usd_translator'] + usd_deps + usd_libs))
