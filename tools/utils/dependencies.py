# vim: filetype=python
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
import os
from . import build_tools
from . import system

def get_boost_lib(env, lib):
    return env['BOOST_LIB_NAME'] % lib

def add_optional_libs(env, libs):
    if env['USD_HAS_PYTHON_SUPPORT']:
        libs += [env['PYTHON_LIBRARY']]
        if env['USD_VERSION_INT'] < 2411:
            libs += [get_boost_lib(env, 'python')]
    
    return libs

def get_tbb_lib(env):
    return env['TBB_LIB_NAME'] % 'tbb'

def add_plugin_deps(env, sources, libs, needs_dl):
    if env['USD_BUILD_MODE'] == 'monolithic':
        usd_deps = [
            env['USD_MONOLITHIC_LIBRARY'],
            get_tbb_lib(env),
        ]
        if needs_dl and system.is_linux:
            usd_deps = libs + ['dl']
        return (sources, add_optional_libs(env, usd_deps))
    else:
        usd_deps = [get_tbb_lib(env)]
        usd_libs, usd_sources = build_tools.link_usd_libraries(env, libs)
        usd_deps = usd_deps + usd_libs
        source_files = sources + usd_sources
        if needs_dl and system.is_linux:
            usd_deps = usd_deps + ['dl']
        return (source_files, add_optional_libs(env, usd_deps))

# Returns a list of usd dependencies and source files.
# This only works with monolithic and shared usd dependencies.
def render_delegate(env, sources):
    usd_libs = [
        'arch',
        'plug',
        'trace',
        'tf',
        'vt',
        'gf',
        'work',
        'hf',
        'hd',
        'sdf',
        'usdImaging',
        'usdLux',
        'pxOsd',
        'cameraUtil',
        'usd', # common/rendersettings_utils.h
        'usdGeom', # common/rendersettings_utils.h
        'usdRender', # common/rendersettings_utils.h
        'pcp', # common
        'usdShade', # common
    ]
    if env['USD_VERSION_INT'] < 2005:
        usd_libs.append('hdx')
    if env['USD_VERSION_INT'] >= 2411:
        usd_libs += ['boost','python',]
    if env['USD_VERSION_INT'] >= 2505:
        usd_libs += ['hdsi','ts','usdSkelImaging']
    return add_plugin_deps(env, sources, usd_libs, True)


# This only works with monolithic and shared usd dependencies.
def ndr_plugin(env, sources):
    usd_libs = [
        'arch',
        'tf',
        'gf',
        'vt',
        'ndr',
        'sdr',
        'sdf',
        'usd',
        'usdGeom', # common
        'usdRender', # common
        'pcp', # common
        'usdShade', # common
    ]
    if env['USD_VERSION_INT'] >= 2411:
        usd_libs += ['boost','python',]
    return add_plugin_deps(env, sources, usd_libs, False)

def usd_imaging_plugin(env, sources):
    usd_libs = [
        'ar',
        'arch',
        'plug',
        'tf',
        'trace',
        'vt',
        'gf',
        'work',
        'ndr',
        'sdf',
        'sdr',
        'hf',
        'hd',
        'usd',
        'usdGeom',
        'usdImaging',
        'usdLux',
        'usdShade',
        'usdRender', # common/rendersettings_utils.h
        'pcp', # common
    ]
    if env['USD_VERSION_INT'] >= 2411:
        usd_libs += ['boost','python',]
    if env['USD_VERSION_INT'] >= 2505:
        usd_libs += ['ts',]
    return add_plugin_deps(env, sources, usd_libs, True)

def scene_index_plugin(env, sources):
    usd_libs = [
        'ar',
        'arch',
        'plug',
        'tf',
        'trace',
        'vt',
        'gf',
        'work',
        'ndr',
        'sdf',
        'sdr',
        'hf',
        'hd',
        'hdsi',
        'usdSkel',
        'usdSkelImaging',
        'usd',
        'usdGeom',
        'usdImaging',
        'usdLux',
        'usdShade',
        'usdRender', # common/rendersettings_utils.h
        'pcp', # common
    ]
    if env['USD_VERSION_INT'] >= 2411:
        usd_libs += ['boost','python',]
    if env['USD_VERSION_INT'] >= 2505:
        usd_libs += ['ts',]
    return add_plugin_deps(env, sources, usd_libs, True)

def scene_delegate(env, sources):
    usd_libs = [
        'arch',
        'js',
        'plug',
        'tf',
        'trace',
        'vt',
        'gf',
        'work',
        'sdf',
        'hf',
        'hd',
    ]
    if env['USD_VERSION_INT'] >= 2411:
        usd_libs += ['boost','python',]
    return add_plugin_deps(env, sources, usd_libs, True)

def translator(env, sources):
    if env['USD_BUILD_MODE'] == 'monolithic':
        usd_deps = [
            'usd_translator',
            env['USD_MONOLITHIC_LIBRARY'],
            get_tbb_lib(env),
        ]
        return (sources, add_optional_libs(env, usd_deps))
    elif env['USD_BUILD_MODE'] == 'static':
        # static builds rely on a monolithic static library
        if system.is_windows:
            usd_deps = [
                '-WHOLEARCHIVE:libusd_m', 
                get_tbb_lib(env),
                'Ws2_32',
                'Dbghelp',
                'Shlwapi', 
                'advapi32' 
            ]
        else:
            usd_deps = [
                'libusd_m', 
                get_tbb_lib(env),
            ]

            if system.is_linux:
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
            'usdUtils',
            'vt',
            'usdLux',
            'gf',
            'usdVol',
            'usdSkel',
            'usdRender',
            'work',
        ]

        if env['USD_VERSION_INT'] >= 2411:
            usd_libs += ['boost','python',]
        
        usd_deps = [get_tbb_lib(env)]

        usd_libs, usd_sources = build_tools.link_usd_libraries(env, usd_libs)
        source_files = sources + usd_sources
        return (source_files, add_optional_libs(env, ['usd_translator'] + usd_deps + usd_libs))
