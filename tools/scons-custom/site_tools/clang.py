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

Tool-specific initialization for clang.

"""

import utils as sa
import os
import glob
import re
import SCons

Version = sa.version.Version

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
    versions = sorted((Version(_) for _ in vlist), reverse=True)
    if not v:
        return versions[0]
    closest_v = None
    V = Version(v)
    V_len = len(V)
    for ver in versions:
        if V[:V_len] == ver[:V_len]:
            closest_v = ver
            break
        if V > ver:
            break
    return closest_v

def detect_clang_install_path_windows():
   if not sa.system.is_windows:
      return []
   paths = os.environ.get('PATH').split(';')

   def open_key(name):
      try:
         return SCons.Util.RegOpenKeyEx(SCons.Util.HKEY_LOCAL_MACHINE, name)
      except WindowsError:
         return None

   def get_value(key, name):
      try:
         v = SCons.Util.RegQueryValueEx(key, name)[0]
      except WindowsError:
         v = None
      return v

   keyname = 'SOFTWARE\\Wow6432Node\\LLVM\\LLVM'
   key = open_key(keyname)
   val = get_value(key,'') if key else None # we want the default key value, so use ''
   if val is not None:
      paths.extend(glob.glob(os.path.join(os.sep, val, 'bin')))
   return paths

def generate(env, version=None):

   # Configure tool names
   clang_extension = {
      'linux'  : '',
      'darwin' : '',
      'windows': '.exe',
   }.get(sa.system.os)
   clang_name = {
      'linux'  : r'^(?P<exec>clang)?$',
      'darwin' : r'^(?P<exec>clang)(?P<suffix>-mp-[0-9]+(\.[0-9]+)*)?$',
      'windows': r'^(?P<exec>clang-cl)\.exe$',
   }.get(sa.system.os)
   macro_version = ['__clang_major__','__clang_minor__','__clang_patchlevel__']
   # Look for clang installations in the PATH and other custom places
   toolchain_path = set(os.environ[sa.system.PATH].split(os.pathsep))
   toolchain_path.update({
      'linux'  : glob.glob(os.path.join(os.sep, 'solidangle', 'toolchain', '*', 'bin')),
      'darwin' : [os.path.join(os.sep, 'opt', 'local', 'bin')] +
                 glob.glob(os.path.join(os.sep, 'usr', 'local', 'Cellar', 'llvm', '*', 'bin')) +
                 glob.glob(os.path.join(os.sep, 'usr', 'local', 'Cellar', 'llvm@*', '*', 'bin')),
      'windows': detect_clang_install_path_windows(),
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
   path = versions_detected.get(str(selected_version))
   if not path:
      versions = sorted(Version(_) for _ in versions_detected.keys())
      raise SCons.Errors.UserError("Can't find Clang %s. " % version +
                                   "Installed versions are [%s]." % (', '.join(str(v) for v in versions)))
   if len(path) > 1:
      # Warn if we found multiple installations of a given version
      class ClangWarning(SCons.Warnings.Warning): pass
      SCons.Warnings.enableWarningClass(ClangWarning)
      SCons.Warnings.warn(ClangWarning, 'Multiple installations for Clang %s in [%s]' % (selected_version, ', '.join(path)))

   clang_path, clang_exec = os.path.split(path[0])
   m = re.match(clang_name, clang_exec)
   exec_name, suffix = 'clang', ''
   if m:
      m = m.groupdict('')
      exec_name = m.get('exec'  , exec_name)
      suffix    = m.get('suffix', suffix   )
   env['CC']  = os.path.join(clang_path, clang_exec)
   env['CXX'] = os.path.join(clang_path, exec_name + '++' + suffix)
   if sa.system.is_windows:
      env['CXX']  = env['CC']
      env['LD']   = os.path.join(clang_path, env.get('LINKER_NAME', 'lld-link') + '.exe')
      env['LINK'] = os.path.join(clang_path, env.get('LINKER_NAME', 'lld-link') + '.exe')
      env['AR']   = os.path.join(clang_path, 'llvm-ar.exe')
      env['ARFLAGS'] = 'rcs'
      env['ARCOM']= "${TEMPFILE('$AR $ARFLAGS $TARGET $SOURCES','$ARCOMSTR')}"

   ######
   ###### TBR: for some reason, adding clang_path to PATH is not letting subsequent executions with this environment to find the executable. Maybe only affects windows?
   ######
   env.PrependENVPath('PATH', clang_path)

   env['LLVM_PATH'] = os.path.realpath(os.path.join(clang_path, os.pardir))
   # Special linker detection in linux
   if sa.system.is_linux:
      # Name of the linker (eg. ld, gold, bfd, lld, etc.). It defaults to "gold"
      linker_name = env.get('LINKER_NAME', 'ld').replace('ld.', '', 1)
      # Regular expresions for detecting the linker version
      regex_version = r'(?:[0-9]+)(?:\.(?:[0-9])+)*'
      linker_regex = {
         'ld'  : re.compile(r'^GNU (ld) (?:(?:\(GNU Binutils\))|(?:version)) ({0})'.format(regex_version)),
         'bfd' : re.compile(r'^GNU (ld) (?:(?:\(GNU Binutils\))|(?:version)) ({0})'.format(regex_version)),
         'gold': re.compile(r'^GNU (gold) \((?:(?:GNU Binutils)|(?:version)) ({0})[\-\._0-9a-zA-Z]*\) ({0})'.format(regex_version)),
         'lld' : re.compile(r'^(LLD) ({0})'.format(regex_version)),
      }.get(linker_name)
      # Search for the specified linker in the PATHs
      linker_detected = set()
      for p in env['ENV']['PATH'].split(os.pathsep) + os.environ['PATH'].split(os.pathsep):
         # The linker name uses to be prefixed with "ld." (eg. ld.gold). Try both.
         for linker_prefix in ['ld.', '']:
            linker = os.path.join(p, '{}{}'.format(linker_prefix, linker_name))
            if os.path.isfile(linker) and linker_regex:
               # Get the version if the found path is a file and we have a proper regex
               error, output = sa.system.execute([linker, '-v'])
               found = linker_regex.search(output[0]) if not error else None
               if found:
                  linker_detected.add((
                     found.group(1).lower(), # name
                     found.group(2),         # version
                     linker                  # path
                  ))
      # Sort detected linkers from newest to oldest
      linker_detected = sorted(linker_detected, key=lambda item: Version(item[1]), reverse=True)
      if not linker_detected:
         raise SCons.Errors.UserError("Can't find linker {}".format(linker_name))
      env['LINKER_NAME']    = linker_detected[0][0]
      env['LINKER_VERSION'] = Version(linker_detected[0][1])
      env['LINKER_PATH']    = linker_detected[0][2]
      env['LD'] = env['LINKER_PATH']
   # Use LLVM's tools if they are present in the detected LLVM's path
   # Get the output after the execution of "kick --version"
   # NOTE: Don't do this in macOS for now.
   # It seems that when building FAT object files (with "-arch x86_64 -arch arm64"),
   # LLVM's ar and ranlib can't generate proper universal static libraries. At
   # least with LLVM 15, they simply archive the FAT object files, instead of
   # archiving the separated arch slices and "lipo" them. 
   if not sa.system.is_darwin:
      for i in ('AR', 'RANLIB'):
         tool = 'llvm-{0}{1}{2}'.format(i.lower(), suffix, clang_extension)
         tool = os.path.join(env['LLVM_PATH'], 'bin', tool)
         if os.path.exists(tool):
            env[i] = tool
   # Check the presence of the LLVM Gold plugin, needed for LTO if we use ld.gold
   if sa.system.is_linux and linker_name == 'gold':
      env['LLVM_GOLD_PLUGIN'] = os.path.join(env['LLVM_PATH'], 'lib', 'LLVMgold.so')
      if not os.path.exists(env['LLVM_GOLD_PLUGIN']):
         raise SCons.Errors.UserError("Can't find LLVM Gold plugin in {}".format(env['LLVM_GOLD_PLUGIN']))
   env['COMPILER_VERSION_DETECTED']  = sa.compiler.detect_version(env, env['CC'], macro_version)
   env['COMPILER_VERSION_INSTALLED'] = [str(v) for v in sorted(Version(_) for _ in versions_detected.keys())]
   [apple] = sa.compiler.get_defines(env, env['CC'], ['__apple_build_version__'])
   if apple:
      env['COMPILER_PREFIX'] = 'apple'

   # If requested, detect and configure the Clang's static analyzer (scan-build)
   # More info at http://clang-analyzer.llvm.org/
   if env.get('STATIC_ANALYSIS'):
      static_analyzer  = ['scan-build']
      static_analyzer += [] # scan-build options
      if os.path.exists(os.path.join(env['LLVM_PATH'], 'bin', static_analyzer[0])):
         env['CC']  = ' '.join(static_analyzer + [env['CC'] ])
         env['CXX'] = ' '.join(static_analyzer + [env['CXX']])

def exists(env):
   return True
