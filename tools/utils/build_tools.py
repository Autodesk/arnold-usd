# vim: filetype=python
# Copyright 2019 Autodesk, Inc.
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

## load our own python modules
from . import system

from itertools import izip

def process_return_code(retcode):
   '''
   translates a process return code (as obtained by os.system or subprocess) into a status string
   '''
   if retcode == 0:
      status = 'OK'
   else:
      if system.is_windows:
         if retcode < 0:
            status = 'CRASHED'
         else:
            status = 'FAILED'
      else:
         # When a signal "N" is raised, the process can
         # return with status "128 + N" or "-N"
         if retcode > 128 or retcode < 0:
            status = 'CRASHED'
         else:
            status = 'FAILED'
   return status 

## Searches for file in the system path. Returns a list of directories containing file
def find_in_path(file):
    path = os.environ['PATH']
    path = string.split(path, os.pathsep)
    return filter(os.path.exists, map(lambda dir, file=file: os.path.join(dir, file), path))

## Returns a list of all files with an extension from the 'valid_extensions' list
def find_files_recursive(path, valid_extensions):
    path = os.path.normpath(path)
    list = []
    for root, dirs, files in os.walk(path):
        if '.svn' in dirs:
            dirs.remove('.svn')
        for f in files:
            if os.path.splitext(f)[1] in valid_extensions:
                # Build the absolute path and then remove the root path, to get the relative path from root
                file = os.path.join(root, f)[len(path) + 1:]
                list += [file]
    return list

## Copy directories recursively, ignoring .svn dirs
## <dest> directory must not exist
def copy_dir_recursive(src, dest):
    for f in os.listdir(src):
        src_path  = os.path.join(src, f)
        dest_path = os.path.join(dest, f)
        if os.path.isdir(src_path):
            if f != '.svn':
                if not os.path.exists(dest_path):
                    os.makedirs(dest_path)
                #shutil.copystat(src_path, dest_path)
                copy_dir_recursive(src_path, dest_path)
        else:
            shutil.copy2(src_path, dest_path)

def get_arnold_version(arnold_include_dir, components = 4):
    '''Obtain Arnold library version by parsing 'ai_version.h'
    '''
    ARCH_VERSION=''
    MAJOR_VERSION=''
    MINOR_VERSION=''
    FIX_VERSION=''

    ai_version_h = os.path.join(arnold_include_dir, 'ai_version.h')
    f = open(ai_version_h, 'r')
    
    while True:
        line = f.readline().lstrip(' \t')
        if line == "":
            # We have reached the end of file.
            break
        if line.startswith('#define'):
            tokens = line.split()
            if tokens[1] == 'AI_VERSION_ARCH_NUM':
                ARCH_VERSION = tokens[2]
            elif tokens[1] == 'AI_VERSION_MAJOR_NUM':
                MAJOR_VERSION = tokens[2]
            elif tokens[1] == 'AI_VERSION_MINOR_NUM':
                MINOR_VERSION = tokens[2]
            elif tokens[1] == 'AI_VERSION_FIX':
                FIX_VERSION = tokens[2].strip('"')
    f.close()

    if (components > 0):
        version = ARCH_VERSION
    if (components > 1):
        version += '.' + MAJOR_VERSION
    if (components > 2):
        version += '.' + MINOR_VERSION
    if (components > 3):
        version += '.' + FIX_VERSION

    return version

def get_usd_version(usd_include_dir, components=3):
    VERSION = [''] * 3

    pxr_h = os.path.join(usd_include_dir, 'pxr', 'pxr.h')
    f = open(pxr_h, 'r')

    while True:
        line = f.readline().lstrip(' \t')
        if line == "":
            # We have reached the end of file.
            break
        if line.startswith('#define'):
            tokens = line.split()
            if tokens[1] == 'PXR_MAJOR_VERSION':
                VERSION[0] = tokens[2]
            elif tokens[1] == 'PXR_MINOR_VERSION':
                VERSION[1] = tokens[2]
            elif tokens[1] == 'PXR_PATCH_VERSION':
                VERSION[2] = tokens[2]
    f.close()

    return '.'.join(VERSION[:components])

def convert_usd_version_to_int(usd_version):
    sum = 0
    for v, m in izip(usd_version.split('.'), [10000, 100, 1]):
        sum += int(v) * m 
    return sum

def add_to_library_path(env, new_path):
    if system.os == 'windows':
        var_name = 'PATH'
    elif system.os == 'darwin':
        var_name = 'DYLD_LIBRARY_PATH'
    else:
        var_name = 'LD_LIBRARY_PATH'

    if env['ENV'].has_key(var_name):
        env['ENV'][var_name] = '%s%s%s' % (new_path, os.pathsep, env['ENV'][var_name])
    else:
        env['ENV'][var_name] = new_path

def set_library_path(env):
    if system.os == 'windows':
        var_name = 'PATH'
    elif system.os == 'darwin':
        var_name = 'DYLD_LIBRARY_PATH'
    else:
        var_name = 'LD_LIBRARY_PATH'

    env['PREVIOUS_LIBRARY_PATH'] = ''
    if os.environ.has_key(var_name):
        env['PREVIOUS_LIBRARY_PATH'] = os.environ[var_name]
    os.environ[var_name] = env['ENV'][var_name]

def reset_library_path(env):
    if env.has_key('PREVIOUS_LIBRARY_PATH'):
        if system.os == 'windows':
            var_name = 'PATH'
        elif system.os == 'darwin':
            var_name = 'DYLD_LIBRARY_PATH'
        else:
            var_name = 'LD_LIBRARY_PATH'
        os.environ[var_name] = env['PREVIOUS_LIBRARY_PATH']

def add_to_program_path(env, new_path):
    if env['ENV'].has_key('PATH'):
        env['ENV']['PATH'] = '%s%s%s' % (new_path, os.pathsep, env['ENV']['PATH'])
    else:
        env['ENV']['PATH'] = new_path

def set_program_path(env):
    env['PREVIOUS_PROGRAM_PATH'] = ''
    if os.environ.has_key('PATH'):
        env['PREVIOUS_PROGRAM_PATH'] = os.environ['PATH']
    os.environ['PATH'] = env['ENV']['PATH']

def reset_program_path(env):
    if env.has_key('PREVIOUS_PROGRAM_PATH'):
        os.environ['PATH'] = env['PREVIOUS_PROGRAM_PATH']

def get_default_path(var, default):
    if var in os.environ:
        return os.environ[var]
    else:
        return default

def get_escaped_path(path):
    if system.os() == 'windows':
        return path.replace("\\", "\\\\")
    else:
        return path

def link_usd_libraries(env, libs):
    lib_prefix = env['USD_LIB_PREFIX']
    usd_lib = env['USD_LIB']
    if env['USD_LIB_AS_SOURCE']:
        return [], [os.path.join(usd_lib, '%s%s%s' % (lib_prefix, lib, system.LIB_EXTENSION)) for lib in libs]
    else:
        return ['%s%s' % (lib_prefix, lib) for lib in libs], []
