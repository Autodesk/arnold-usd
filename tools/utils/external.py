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
from SCons.Script import *

from . import system
from . import compiler
from . import elf
from .version import Version

def detect_glibc_version    (env, bin): return compiler.detect_version(env, bin, ['__GLIBC__','__GLIBC_MINOR__'])
def detect_libstdcxx_version(env, bin):
   if system.is_linux:
      s, o = system.execute([bin, '-print-file-name=libstdc++.so'], env=env['ENV'])
      if s == 0 and len(o) == 1:
         try:
            return elf.get_maximum_symbol_version(o[0]).get('GLIBCXX', Version(None))
         except:
            pass
   return Version(None)

def configure(env):
   ## Create a dictionary of all our libs so we can pass them around
   ## to child SConscript's easily
   libs = {}

   # Determine the internal name of the external libraries, the path under the
   # contrib folder, and the libraries we will be linking
   ext_config = (
   #   name      , location     , library
      ('oiio'    , 'OpenImageIO', 'OpenImageIO'     ),
      ('ocio'    , 'OpenColorIO', 'OpenColorIO'     ),
      ('tcmalloc', 'tcmalloc'   , 'tcmalloc_minimal'),
      ('rlm'     , 'rlm'        , 'rlm.a'             if not system.is_windows else 'rlmclient_md gdi32 wbemuuid.lib'),
      ('clm'     , 'clm'        , 'AdClmHub' + ('_1' if system.is_windows else '')),
      ('pit'     , 'pit'        , 'adlmPIT'),
      ('m_intel' , 'IntelC++'   , 'libimf.a libirc.a' if not system.is_windows else 'libmmds.lib libirc.lib svml_dispmt.lib'),
      ('m_amd'   , 'amdlibm'    , 'amdlibm'),
      ('m_system', ''           , 'm'),
      ('tbb'     , 'tbb'        , 'tbb'),
      ('blosc'   , 'blosc'      , 'blosc'),
      ('boost'   , 'boost'      , 'boost_iostreams'),
      ('openvdb' , 'openvdb'    , 'openvdb'),
      ('zlib'    , 'zlib'       , ''),
      ('curl'    , 'curl'       , 'libcurl.a' if not system.is_windows else 'libcurl.lib ssleay32.lib libeay32.lib libssh2.lib Crypt32.lib Wldap32.lib'),
   )
   # We don't need to link to the following libraries
   ext_no_needs_link = ('zlib',)
   # Add the previous libraries to the "libs" dictionary
   for ext, ext_loc, ext_lib in ext_config:
      if ext in ['clm', 'pit'] and not env['USE_CLM']:
         continue
      ext_incpath = os.path.join('#contrib', ext_loc, 'include')
      ext_libpath = os.path.join('#contrib', ext_loc, 'lib', system.os)
      ext_incpath = ext_incpath if os.path.exists(Dir(ext_incpath).abspath) else ''
      ext_libpath = ext_libpath if os.path.exists(Dir(ext_libpath).abspath) else ''
      libs[ext]   = ExternalLibrary(ext_lib, ext_incpath, ext_libpath, '', '', (ext not in ext_no_needs_link))

   if env['USE_OSL']:
      osl_includepath = '#contrib/OSL/include'
      osl_libpath = '#contrib/OSL/lib/%s' % system.os
      osl_binpath = '#contrib/OSL/bin/%s' % system.os
      libs['osl']  = ExternalLibrary('oslexec oslquery oslcomp', osl_includepath, osl_libpath, defines='OSL_STATIC_BUILD OIIO_STATIC_BUILD')

   if system.is_windows:
      libs['dbghelp'] = ExternalLibrary('Dbghelp')
      libs['gl']      = ExternalLibrary('opengl32')
      if env['USE_AZURE']:
         libs['curl'].defines = 'CURL_STATICLIB'
   elif system.is_linux:
      libs['pthread'] = ExternalLibrary('pthread')
      libs['X11']     = ExternalLibrary('X11 xcb Xau')
      libs['gl']      = ExternalLibrary('GL', needs_link=False)
      libs['dl']      = ExternalLibrary('dl')
      # real time extensions library is needed for python to find libai.so
      libs['rt']      = ExternalLibrary('rt')
      # needed for __builtin_cpu_supports on some systems, see #5662
      libs['gcc']     = ExternalLibrary('gcc')
   elif system.is_darwin:
      libs['pthread'] = ExternalLibrary('pthread')

   env['EXTLIBS'] = libs

   # we globally choose the "math" library between either libimf, amdlibm+m, or
   # none of them
   if env['LIBM'] != 'none':
      env['EXTLIBS']['m_{}'.format(env['LIBM'])].attach(env)


## this class will represent the use of a 3rd party library
class ExternalLibrary:
   def __init__(self, libs, includepath='', libpath='', defines='', linkflags='', needs_link=True):
      ## the name of the lib is the only required argument
      self.libs = libs

      ## all of these are optional
      self.defines     = defines
      self.includepath = includepath
      self.libpath     = libpath
      self.linkflags   = linkflags
      self.needs_link  = needs_link

   ## Attach this library to the specific environment
   def attach(self, env):
      ## Configure the compiler
      if self.defines != '':
         ## configure the preprocessor
         env.Append(CPPDEFINES=env.Split(self.defines))
      if self.includepath != '':
         ## configure the path to the headers
         env.Append(CPPPATH=env.Split(self.includepath))

      ## defer the linker config to this other method
      self.linkwith(env)

   def linkwith(self, env):
      ## Configure the linker
      if self.needs_link:
         env.Append(LIBS=env.Split(self.libs))
         if self.libpath != '':
            ## configure the path to the libraries
            env.Append(LIBPATH=env.Split(self.libpath))
         if self.linkflags != '':
            env.Append(LINKFLAGS=env.Split(self.linkflags))
