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
"""

Tool-specific initialization for clang.

"""

import utils as sa
import os
import glob
import re
from sets import Set
import SCons

def split_version(v):
    """Splits a version string 'a.b.c' into an int list [a, b, c]"""
    try:
        s = [int(x) for x in v.split('.')]
    except ValueError:
        return []
    return s

def closest_version(v, vlist):
    """See if we can match v (string) in vlist (list of strings)
    If user requests v=13, it will return the greatest 13.x version.
    If user requests v=13.1, it will return the greatest 13.1.x version, and so on"""
    if not v:
        return vlist[0]
    s = split_version(v)
    l = len(s)
    # Try to match the latest installed version of the requested version
    if l > 0:
        for vi in vlist:
            S = split_version(vi)
            if l > len(S):
                continue
            n = 0
            for i in range(l):
                if s[i] == S[i]: n = n + 1
                else:            break
            if n == l:
                return vi
    return None

def generate(env, version=None):
   # Configure tool names
   clang_name = {
      'linux' : r'^(?P<exec>clang)?$',
      'darwin': r'^(?P<exec>clang)(?P<suffix>-mp-.\..)?$',
   }.get(sa.system.os)
   macro_version = ['__clang_major__','__clang_minor__','__clang_patchlevel__']
   # Look for clang installations in the PATH and other custom places
   toolchain_path = Set(os.environ[sa.system.PATH].split(os.pathsep))
   toolchain_path.update({
      'linux' : glob.glob(os.path.join(os.sep, 'solidangle', 'toolchain', '*', 'bin')),
      'darwin': [os.path.join(os.sep, 'opt', 'local', 'bin')],
   }.get(sa.system.os, []))
   versions_detected = {}
   for p in toolchain_path:
      if os.path.isdir(p):
         clang_name_match = [f for f in os.listdir(p) if re.search(clang_name, f)]
         for clang in clang_name_match:
            clang_path = os.path.realpath(os.path.join(p, clang))
            if os.path.isfile(clang_path):
               v = repr(sa.compiler.detect_version(env, clang_path, macro_version))
               paths = versions_detected.get(v, [])
               if clang_path not in paths: paths.append(clang_path)
               versions_detected[v] = paths

   if not versions_detected.keys():
      raise SCons.Errors.UserError("Can't find Clang.")
   # Try to match the closest detected version
   selected_version = closest_version(version, versions_detected.keys())
   path = versions_detected.get(selected_version)
   if not path:
      raise SCons.Errors.UserError("Can't find Clang %s. " % version +
                                   "Installed versions are [%s]." % (', '.join(versions_detected.keys())))
   if len(path) > 1:
      # Warn if we found multiple installations of a given version
      class ClangWarning(SCons.Warnings.Warning): pass
      SCons.Warnings.enableWarningClass(ClangWarning)
      SCons.Warnings.warn(ClangWarning, 'Multiple installations for Clang %s in [%s]' % (selected_version, ', '.join(path)))

   clang_path, clang_exec = os.path.split(path[0])
   m = re.match(clang_name, clang_exec)
   exec_name, suffix = 'clang', ''
   if m:
      m = m.groupdict()
      exec_name = m.get('exec', exec_name)
      suffix    = m.get('suffix', suffix )
   env['CC']  = clang_exec
   env['CXX'] = exec_name + '++' + suffix
   if sa.system.is_linux:
      # In order to use LTO, we need the gold linker, which is able to load plugins
      env['LD']  = 'ld.gold'

   env.PrependENVPath(sa.system.PATH, clang_path)
   # Use LLVM's tools if they are present in the detected LLVM's path
   for i in ('AR', 'RANLIB'):
      tool = 'llvm-{0}{1}'.format(i.lower(), suffix)
      if os.path.exists(os.path.join(clang_path, tool)):
         env[i] = tool
   # Check the presence of the LLVM Gold plugin, needed for LTO
   if sa.system.is_linux:
      env['LLVM_GOLD_PLUGIN'] = os.path.join(clang_path, os.pardir, 'lib', 'LLVMgold.so')
      if not os.path.exists(env['LLVM_GOLD_PLUGIN']):
         raise SCons.Errors.UserError("Can't find LLVM Gold plugin")
   env['COMPILER_VERSION_DETECTED']  = sa.compiler.detect_version(env, env['CC'], macro_version)
   env['COMPILER_VERSION_INSTALLED'] = versions_detected.keys()
   [apple] = sa.compiler.get_defines(env, env['CC'], ['__apple_build_version__'])
   if apple:
      env['COMPILER_PREFIX'] = 'apple'

   # If requested, detect and configure the Clang's static analyzer (scan-build)
   # More info at http://clang-analyzer.llvm.org/
   if env['STATIC_ANALYSIS']:
      static_analyzer  = ['scan-build']
      static_analyzer += [] # scan-build options
      if os.path.exists(os.path.join(clang_path, static_analyzer[0])):
         env['CC']  = ' '.join(static_analyzer + [env['CC'] ])
         env['CXX'] = ' '.join(static_analyzer + [env['CXX']])

def exists(env):
   return True
