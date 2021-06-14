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
from builtins import next
from builtins import hex
from builtins import zip
from builtins import str
from builtins import range
from SCons.Script import *

import os, shlex, subprocess, tempfile

from . import system
from .version import Version

allowed = {
   'darwin' : ('clang:4.0.0', 'gcc'),
   'linux'  : ('clang:4.0.0', 'gcc', 'icc'),
   'windows': ('icc:17.0.2',),
}.get(system.os, None)

def _dump_macros(env, bin):
   ## Create a temporal c++ compilation unit
   f = tempfile.NamedTemporaryFile(suffix='.cc', delete=False)
   f.write('#include <cstdlib>\n')
   f.close()
   ## Dump macro definitions
   cmd = [bin, '-dM', '-E', '-x', 'c++', f.name]
   _, lines = system.execute(cmd, env=env['ENV'])
   ## Remove the temporal c++ compilation unit
   os.remove(f.name)
   return lines

def get_defines(env, bin, m):
   r = [None] * len(m)
   for l in _dump_macros(env, bin):
      ls = l.split()
      for i in range(len(r)):
         if ls[:2] == ['#define', m[i]]: r[i] = ls[2]
   return r

def detect_version(env, bin, m):
   '''
   Detect version by parsing predefined macros
   '''
   v = [None] * len(m)
   for l in _dump_macros(env, bin):
      ls = l.split()
      for i in range(len(v)):
         if ls[:2] == ['#define', m[i]] and v[i] == None: v[i] = int(ls[2])
   n = next((i for i in range(len(v)) if v[i] == None), len(v))
   return Version(v[:n])

def detect_gcc_version  (env, bin): return detect_version(env, bin, ['__GNUC__','__GNUC_MINOR__' ,'__GNUC_PATCHLEVEL__' ])
def detect_clang_version(env, bin): return detect_version(env, bin, ['__clang_major__','__clang_minor__','__clang_patchlevel__'])

def validator(key, val, env):
   ## Convert the tuple of strings with allowed compilers and versions ...
   ##    ('clang:3.4.0', 'icc:14.0.2', 'gcc')
   ## ... into a proper dictionary ...
   ##    {'clang' : '3.4.0', 'icc' : '14.0.2', 'gcc' : None}
   allowed_versions  = {i[0] : i[1] if len(i) == 2 else None for i in (j.split(':') for j in allowed)}
   ## Parse the compiler format string <compiler>:<version>
   compiler = val.split(':')
   ## Validate if it's an allowed compiler
   if compiler[0] not in list(allowed_versions.keys()):
      m = 'Invalid value for option {}: {}.  Valid values are: {}'
      raise SCons.Errors.UserError(m.format(key, val, list(allowed_versions.keys())))
   ## Set valid compiler name and version. If the version was not specified
   ## we will set the default version which we use in the official releases,
   ## so the build will fail if it cannot be found.
   env['_COMPILER'] = compiler[0]
   env['_COMPILER_VERSION'] = len(compiler) == 2 and compiler[1] or allowed_versions[compiler[0]]

def configure(env):

   # Get an alias dictionary for the Scons construction environment, so we can
   # de-reference it with the '**' operator in some function calls.
   env_dict = env.Dictionary()

   # Initialize a dictionary of compiler flags. Once every flag is collected in
   # the following lists, we will pass this dict to the env.Append() function
   # for appending them to the SCons construction environment 'env'.
   compiler_flags = {
      'CPPDEFINES' : [], # C/C++ preprocessor directives
      'CCFLAGS'    : [], # C compiler flags
      'CXXFLAGS'   : [], # C++ compiler flags
      'LINKFLAGS'  : [], # Linker flags
      'RPATH'      : [], # Runtime paths
   }
   # Create shorter aliases for every entry in the previous dictionary, so we
   # can write less verbose code.
   CPPDEFS  = compiler_flags['CPPDEFINES']
   CFLAGS   = compiler_flags['CCFLAGS'   ]
   CXXFLAGS = compiler_flags['CXXFLAGS'  ]
   LDFLAGS  = compiler_flags['LINKFLAGS' ]
   RPATH    = compiler_flags['RPATH'     ]

   # Preconfigure some common characters used in the compilers flags for
   # negating, prefixing, etc.
   # For example, in Windows, flags are prefixed by "/" instead of "-" (Linux).
   # "Q" prefix is also common in windows and flags are commonly negated with
   # a trailing "-", whereas Linux/OSX uses "no-" for negating features.
   flag_prefix = '/' if system.is_windows else '-'
   flag_Q      = 'Q' if system.is_windows else ''
   flag_neg    = '-' if system.is_windows else 'no-'
   flag_sep    = ':' if system.is_windows else '='
   def enable_in(mode):
      # Return the corresponding charater for enabling a flag if env['MODE'] is
      # 'mode'. Disable otherwise in the rest of modes. This makes sense only
      # for compiler features that can be enabled/disabled with the same flag
      return flag_neg if env['MODE'] not in [mode] else ''

   ## Detect compiler selected/installed versions ##############################
   if env['_COMPILER'] == 'gcc':
      env['CC']  = 'gcc'
      env['CXX'] = 'g++'
      env['COMPILER_VERSION_DETECTED'] = detect_gcc_version(env, env['CXX'])
   elif env['_COMPILER'] == 'clang':
      env.Tool('clang', version = env['_COMPILER_VERSION'])
   elif env['_COMPILER'] == 'icc':
      env.Tool('intelc', version = env['_COMPILER_VERSION'])
      # If provided, use the following GCC toolchain to set up the environment
      # for C/C++ compilations, otherwise ICC will use the system GCC.
      gcc_toolchain = env.get('GCC_SUPPORT_ROOT')
      if gcc_toolchain:
         CFLAGS  += ['cxxlib={}'.format(gcc_toolchain)]
         LDFLAGS += ['cxxlib={}'.format(gcc_toolchain)]

   ## Inject compiler name and version with C/C++ preprocessor directives
   compiler_prefix  = env.get('COMPILER_PREFIX', '')
   compiler_prefix += '-' if compiler_prefix else ''
   compiler_name    = env['_COMPILER']
   compiler_version = env['COMPILER_VERSION_DETECTED']
   CPPDEFS += [{'__AI_COMPILER__'       : '\\"{0}{1}\\"'.format(compiler_prefix, compiler_name)}]
   CPPDEFS += [{'__AI_COMPILER_MAJOR__' : compiler_version[0]}]
   CPPDEFS += [{'__AI_COMPILER_MINOR__' : compiler_version[1]}]
   CPPDEFS += [{'__AI_COMPILER_PATCH__' : compiler_version[2]}]
   CPPDEFS += [{'__AI_COMPILER_{}__'.format(compiler_name.upper()) : \
                sum(a*b for a,b in zip(compiler_version[:3], [10000,100,1]))}]

   ## Conform to a given C++ language standard #################################
   if (compiler_name, env['STDCXX']) != ('icc', '98'):
      CXXFLAGS += ['{0}std=c++{STDCXX}'.format(flag_Q, **env_dict)]
   # NOTE (#4713): Full C++11 support in darwin requires using LLVM's libc++
   # standard library instead of GNU's libstdc++.
   if system.is_darwin:
      CXXFLAGS += ['stdlib=libc++']
      LDFLAGS  += ['stdlib=libc++']
   # NOTE (#4986): In the GCC 5.1 release libstdc++ introduced a new library ABI
   # that includes new implementations of std::string and std::list. The
   # _GLIBCXX_USE_CXX11_ABI macro controls whether the declarations in the
   # library headers use the old or new ABI.
   if system.is_linux:
      CPPDEFS += [{'_GLIBCXX_USE_CXX11_ABI' : 0}]

   ## Control warning and error diagnostics and verbosity ######################
   if env['WARN_LEVEL'] == 'none':
      # Disables all warning messages
      CFLAGS += ['w']
   else:
      # NOTE (icc): On Windows, /Wall is equivalent to /W4. It enables
      # diagnostics for all level 3 warnings plus informational warnings and
      # remarks. However, on icc Linux/OSX, it displays all errors and some of
      # the warnings that are typically reported by gcc option -Wall, so we
      # have to might explicitely enable the remarks (-Wremarks) if wanted.
      CFLAGS += ['Wall']
      # Enable also some extra warning flags that are not enabled by -Wall
      CFLAGS += ['Wextra'] if not system.is_windows else []
      # Changes all warnings to errors if we want to be strict
      if env['WARN_LEVEL'] == 'strict':
         CFLAGS += ['Werror'] if not system.is_windows else ['WX']

   # Disable selected diagnostics by compiler / platform

   if compiler_name in ['clang', 'gcc']:
      # Disable selected warnings from -Wextra
      CFLAGS += ['Wno-unused-parameter']
      CFLAGS += ['Wno-unused-local-typedef']

   if compiler_name == 'gcc':
      # Disable selected warnings from -Wextra
      CFLAGS += ['Wno-unused-variable']
      CFLAGS += ['Wno-strict-aliasing']
      CFLAGS += ['Wno-format-zero-length']
      CFLAGS += ['Wno-unused-but-set-parameter'] # in gcc 4.6+
      CFLAGS += ['Wno-maybe-uninitialized']      # in gcc 4.7+

   if compiler_name == 'clang':
      # Some automatically generated .cpp files use the "register" keyword (flex
      # and bison). Such "register" keyword is deprecated in C++11 (and it will
      # be removed in C++17) so clang is currently generating a warning.
      # We will disable that warning if clang >= 3.4 or apple-clang >= 5.1
      if (compiler_prefix == 'apple' and compiler_version >= Version('5.1')) or \
         (compiler_prefix != 'apple' and compiler_version >= Version('3.4')):
         CXXFLAGS += ['Wno-deprecated-register']

   if compiler_name == 'icc':
      # Disables the following warnings:
      warn_ids = [
           111, # statement is unreachable (needed for parser generated code)
           181, # compatibility checks between variable type and printf mask
           280, # selector expression is constant
           367, # duplicate friend declaration (OpenVDB headers)
           504, # initial value of reference to non-const must be an lvalue (Boost headers)
           869, # parameter XXXX was never referenced
          1879, # unimplemented pragma ignored (TBB headers)
          2586, # decorated name length exceeded, name was truncated (OpenVDB headers)
          2960, # class allocated with new might not be aligned (we should review this)
          3280, # declaration hides existing member or variable (we should review this)
          3346, # (remark) Dynamic exception specifications are deprecated (OpenEXR headers)
         11074, # (remark) Inlining inhibited by limit max-size|max-total-size
         11075, # (remark) To get full report use -Qopt-report:4 -Qopt-report-phase ipo (Windows)
         11076, # (remark) To get full report use -qopt-report=4 -qopt-report-phase ipo (Linux)
      ]
      CFLAGS += ['{}diag-disable{}{}'.format(flag_Q, flag_sep, ','.join(str(i) for i in warn_ids))]
      # Disables diagnostic information reported by the vectorizer
      if compiler_version >= Version('15.0'):
         CFLAGS += ['{}diag-disable{}vec'.format(flag_Q, flag_sep)]
      else:
         CFLAGS += ['{}vec-report{}0'.format(flag_Q, flag_sep)]

      if system.is_windows and (Version(env['MSVC_VERSION']) == Version('14.0')) \
         and compiler_version >= Version('15.0.4'):
         # Visual Studio 2015 Update 1 changed some builtin intrinsics into
         # macros, giving some errors about undefined symbols when using icc 15 and 16
         # https://software.intel.com/en-us/articles/limits1120-error-identifier-builtin-nanf-is-undefined
         # This was fixed in icc 16.0.2
         if compiler_version < Version('16.0.2'):
            CPPDEFS += [{'__builtin_huge_val()' : 'HUGE_VAL' }]
            CPPDEFS += [{'__builtin_huge_valf()': 'HUGE_VALF'}]
            CPPDEFS += [{'__builtin_nan'        : 'nan'      }]
            CPPDEFS += [{'__builtin_nanf'       : 'nanf'     }]
            CPPDEFS += [{'__builtin_nans'       : 'nan'      }]
            CPPDEFS += [{'__builtin_nansf'      : 'nanf'     }]
         # Visual Studio 2015 Update 2 introduced a new syntax "__is_assignable" that Intel
         # Compiler 16.0 update 2 doesn't yet support. This should be fixed in update 3. Until
         # then we can work around it by defining a corresponding macro. See here for more info:
         # https://software.intel.com/en-us/forums/intel-c-compiler/topic/623368
         # This was fixed in icc 16.0.3
         if compiler_version < Version('16.0.3'):
            CPPDEFS += [{'__is_assignable': '__is_trivially_assignable'}]

   if system.is_windows:
      # disables some CRT secure warnings. For the time being this is disabling
      # warning 1786 for the deprecated function sprintf()
      CPPDEFS += ['_CRT_SECURE_NO_WARNINGS']
      # Since we are not generating debug info when manually compiling the
      # external libraries we need this flag in order to disable whe linker
      # warning LNK4099 "PDB 'filename' was not found with 'object/library' or
      # at 'path'; linking object as if no debug info".
      LDFLAGS += ['ignore:4099']

   # Enable colors in warning and error diagnostics
   if env['COLOR_CMDS']:
      if compiler_name == 'clang':
         CFLAGS += ['fcolor-diagnostics']
      elif compiler_name == 'gcc' and compiler_version >= Version('4.9'):
         CFLAGS += ['fdiagnostics-color']

   if compiler_name == 'icc':
      # NOTE (#3323): Tell the compiler to display certain information to the
      # console output window. In this case do not display the name of the file
      # being compiled
      CFLAGS += ['watch:nosource']

   if env['SHOW_CMDS'] and not system.is_windows:
      # NOTE (#5196): Tell the compiler to show commands to run and use verbose
      # output.
      CFLAGS  +=['v']
      LDFLAGS +=['v']

   ## Compiler optimizations ###################################################
   if env['MODE'] in ['opt', 'profile', 'dev']:
      # NOTE (#3855): clang's "-O4" flag has been always equivalent to
      # "-O3 -flto". But from version 3.4 "-O4" was removed. So we better use
      # the common denominator
      CFLAGS += ['O3']
      # In icc enable additional interprocedural optimizations for single-file
      # compilation.
      CFLAGS += ['{}ip'.format(flag_Q)] if compiler_name in ['icc'] else []

   elif env['MODE'] in ['debug']:
      # In debug mode we will disable all optimizations (-O0 in Linux/OSX and
      # /Od in windows)
      CFLAGS += ['O{}'.format('0' if not system.is_windows else 'd')]

   # Unroll loops whose number of iterations can be determined at compile time
   # or upon entry to the loop. Do it in 'opt' and 'profile' modes. icc has the
   # alternate notation /Qunroll, -unroll for Windows and Linux/OSX platforms.
   if env['MODE'] in ['opt', 'profile']:
      CFLAGS += {
         'gcc'  : ['funroll-loops'],
         'clang': ['funroll-loops'],
         'icc'  : ['{}unroll'.format(flag_Q)],
      }.get(compiler_name, [])

   # NOTE (#4228): Disabling math errno checks in non-debug builds improve
   # performance since we never use errno. This flag is available for gcc, clang
   # and icc in Linux/OSX
   if not system.is_windows:
      CFLAGS += ['f{}math-errno'.format(enable_in('debug'))]

   # Improve the consistency of floating-point tests for equality and
   # inequality by disabling optimizations that could change the precision of
   # floating-point calculations. This flag is only available in icc and msvc.
   flag_fm = 'fp{}{}precise'
   if system.is_windows:        CFLAGS += [flag_fm.format(''      , flag_sep)]
   elif compiler_name == 'icc': CFLAGS += [flag_fm.format('-model', flag_sep)]

   # Improves floating-point consistency. It ensures out-of-range check of
   # operands of transcendental functions and improves the accuracy of
   # floating-point compares. This option disables fewer optimizations and
   # has less impact on performance than /fp:precise. This flag is only
   # available in icc.
   if compiler_name == 'icc':
      CFLAGS += ['Qprec' if system.is_windows else 'mp1']

   # NOTE (#3662): In non-'debug' modes, we disable the generation of code that
   # detects some types of buffer overruns that overwrite a function's return
   # address, exception handler address, or certain types of parameters, to gain
   # a significant performance gain.
   flag_sp = 'GS{}' if system.is_windows else 'f{}stack-protector'
   CFLAGS += [flag_sp.format(enable_in('debug'))]

   # NOTE (#3607): If we are generating an 'opt' build, don't keep the frame
   # pointer in a general-purpose register for functions that don't need one.
   # This avoids the instructions to save, set up and restore frame pointers; it
   # also makes an extra register available in many functions.
   # This flag is /Oy[-] in Windows and -f[no-]omit-frame-pointer in Linux/OSX
   # This flag is also automatically enabled/disabled in certain -O<level>
   # optimization levels, but we prefer to explicitly specify it.
   flag_fp = 'Oy{}' if system.is_windows else 'f{}omit-frame-pointer'
   CFLAGS += [flag_fp.format(enable_in('opt'))]

   ## Linker optimizations #####################################################

   # Make clang linker always use the LLVM Gold plugin, in order to understand
   # byte code object files from external static libraries
   if compiler_name in ['clang'] and system.is_linux:
      LDFLAGS += ['Wl,-plugin={LLVM_GOLD_PLUGIN}'.format(**env_dict)]
      LDFLAGS += ['Wl,-plugin-opt=mcpu=x86-64']

   if env['MODE'] in ['opt', 'profile']:

      if compiler_name in ['gcc', 'clang']:
         if compiler_name in ['clang']:
            CFLAGS += ['flto']
         # FIXME: Disabled LTO in GCC:
         # Currently, GCC does not support combining LTO object files compiled
         # with different set of the command line options into a single binary.
         # So we will get linker errors due to precompiled external libs.
         # NOTE: we can re-enable this, once externals can be built within SCons
         if False:
            if compiler_name in ['gcc']:
               # Use the ld.gold for using the LTO plugin
               LDFLAGS += ['Wl,-fuse-ld=gold']
               # Use wrapper tools for LTO support to pass a plugin to the
               # ar/nm/ranlib programs
               # NOTE: this should be moved to the GCC detection section
               for i in ('NM', 'AR', 'RANLIB'):
                  tool = 'gcc-{0}'.format(i.lower())
                  if env.Detect(tool):
                     env[i] = tool
               CFLAGS  += ['flto']
               LDFLAGS += ['Wl,-flto']

      if compiler_name in ['icc']:
         # Enable interprocedural optimization between files (multifile IPO).
         # The compiler will perform inline function expansion for calls to
         # functions defined in separate files.
         CFLAGS += ['{}ipo'.format(flag_Q)]

      if system.is_windows:
         # Eliminates functions and data that are never referenced (REF) ands
         # perform identical COMDAT folding (ICF).
         LDFLAGS += ['OPT:REF,ICF']

   ## Platform optimizations ###################################################
   # Indicate to the compiler the feature set that it may target, including
   # which instruction sets it may generate.
   # NOTE (#5665): We moved to SSE 4.1
   instruction_set = env['INSTRUCTION_SET']

   # NOTE (#5476): We have to revert to SSE4.1 in windows due to a bug detected
   # in icc 16.0.3, which is also present in icc 17.0.0 (see comment:7)
   affected_by_5476 = ['16.0.3', '16.0.4', '17.0.0', '17.0.1']
   if system.is_windows and (instruction_set == 'sse4.2') and (compiler_name == 'icc') and \
      (compiler_version in [Version(x) for x in affected_by_5476]):
      instruction_set = 'sse4.1'

   if system.is_darwin : CFLAGS += ['m{}'.format(instruction_set)]
   if system.is_linux  : CFLAGS += ['m{}'.format(instruction_set)]
   if system.is_windows: CFLAGS += ['arch:{}'.format(instruction_set.upper())]

   ## General platform related options #########################################
   if system.is_windows:
      CPPDEFS += ['_WINDOWS', '_WIN32', 'WIN32', '_WIN64']

      # We explicitly target Windows 7
      windows_version = '7'
      windows_target  = {
         '10'   : 0x0A000000, # Windows 10
         '8.1'  : 0x06030000, # Windows 8.1
         '8'    : 0x06020000, # Windows 8
         '7'    : 0x06010000, # Windows 7
         'vista': 0x06000000, # Windows Vista
         'xp'   : 0x05010000, # Windows XP
      }.get(windows_version)
      CPPDEFS += [{'NTDDI_VERSION' : hex(windows_target)}]
      CPPDEFS += [{'_WIN32_WINNT'  : hex(windows_target >> 16)}]

      CPPDEFS += [{'OIIO_STATIC_BUILD' : 1}]
      CPPDEFS += ['PERFTOOLS_DLL_DECL']

      # Disable checked iterators
      CPPDEFS += [{'_SECURE_SCL' : 0}]
      # Causes the application to use the NON-DEBUG multithread-specific and
      # DLL-specific version of the run-time library. Defines "_MT" and "_DLL"
      # and causes the compiler to place the library name MSVCRT.lib into the
      # .obj file. Applications compiled with this option are statically linked
      # to MSVCRT.lib. This library provides a layer of code that enables the
      # linker to resolve external references. The actual working code is
      # contained in MSVCR<versionnumber>.DLL, which must be available at run
      # time to applications linked with MSVCRT.lib.
      CFLAGS += ['MD']
      # Enable the ISO-standard C++ exception-handling model that catches C++
      # exceptions only and tells the compiler to assume that functions declared
      # as extern "C" never throw a C++ exception.
      CFLAGS += ['EHsc']

      # Generates complete debug information. In opt and profile modes we will
      # generate it in a separate .pdb file, while in dev and debug modes it
      # will be included in the object files.
      if env['MODE'] in ['opt', 'profile']:
         CFLAGS += ['debug:full' if compiler_name == 'icc' else 'Zi']
      elif env['MODE'] in ['dev', 'debug']:
         CFLAGS += ['Z7']

      # Controls how the linker handles incremental linking. We just want an
      # incremental link in 'debug' mode. Otherwise we will perform a full link
      if env['MODE'] in ['opt', 'profile', 'dev']:
         LDFLAGS += ['INCREMENTAL:NO']
      else:
         LDFLAGS += ['INCREMENTAL']
      # Tell the linker that the application can handle addresses larger than
      # 2 gigabytes. In the 64-bit compilers, this option is enabled by default,
      # FIXME: So maybe it's useless at this point?
      LDFLAGS += ['LARGEADDRESSAWARE']
      # Specifys that the linker should create a side-by-side manifest file.
      LDFLAGS += ['MANIFEST']
      # Exclude mmd, cmt and svml from the list of default libraries. We will use ...
      # ... libmmd from contrib/IntelC++/lib when building arnold with '/MD'
      # ... libcmt from contrib/tcmalloc/lib when building arnold with tcmalloc
      #     and '/MT'
      # ... svml_dispmd from contrib/IntelC++/lib when building arnold with '/MD'
      LDFLAGS += ['NODEFAULTLIB:libmmd']
      LDFLAGS += ['NODEFAULTLIB:libirc']
      LDFLAGS += ['NODEFAULTLIB:libcmt']
      LDFLAGS += ['NODEFAULTLIB:svml_dispmd']
      # Tell the linker to put the debugging information into a program database
      # (.PDB)
      LDFLAGS += ['DEBUG']
   else:
      # NOTE (#2827): Always include all debugging symbols and extra
      # information, such as all the macro definitions. We will strip them all
      # after dumping the symbol table.
      # In windows, icc/msvc have the equivalent /Z7, but we are currently using
      # /debug:full (/Zi) for generating debug info in a project database (PDB)
      # file
      CFLAGS  += ['g3']
      LDFLAGS += ['g3']

   if system.is_darwin:
      CPPDEFS += ['_DARWIN']
      CPPDEFS += ['_DARWIN_UNLIMITED_STREAMS'] # so we can change number of file handles (#5236)
      # Minimum compatibility with Mac OSX 10.8
      CFLAGS  += ['mmacosx-version-min=10.8']
      LDFLAGS += ['mmacosx-version-min=10.8']
      CFLAGS  += ['isysroot {SDK_PATH}/MacOSX{SDK_VERSION}.sdk/'.format(**env_dict)]
      LDFLAGS += ['isysroot {SDK_PATH}/MacOSX{SDK_VERSION}.sdk/'.format(**env_dict)]
      LDFLAGS += ['framework CoreServices']
      # leave some room to change dylib paths with install_name_tool
      LDFLAGS += ['headerpad_max_install_names']

   if system.is_linux:
      CPPDEFS += ['_LINUX']
      # Hide all internal symbols (the ones without AI_API decoration)
      # FIXME (#4133): Symbols should be hidden on OSX as well.
      CFLAGS   += ['fvisibility=hidden']
      LDFLAGS  += ['fvisibility=hidden']
      # Hide all symbols imported from external static libraries.
      # Flag '-Wl,--exclude-libs,ALL' is only supported in linux.
      # NOTE (#2294, #2632): We don't use '-Wl,-s' option anymore, since we can
      # do it as a post-process with the 'strip -s' command
      LDFLAGS += ['Wl,--exclude-libs,librlm.a:libOpenImageIO.a:libOpenColorIO.a']
      # Hardcode '.' directory in RPATH in linux
      # NOTE (#4219): In order to use the RPATH (instead of the RUNPATH) in
      # newer linkers we have to specifically use the flag --disable-new-dtags
      RPATH += [env.Literal('\\$$ORIGIN')]
      LDFLAGS += ['Wl,--disable-new-dtags']

   ## Misc. options ############################################################

   if compiler_name in ['icc']:
      # Statically link Intel libraries so our libraries work on machines
      # without intel redistributables
      if not system.is_windows:
         LDFLAGS += ['static-intel']
      # ... But we don't want Intel C/C++ language extensions such as array
      # notation, Cilk, etc. since with some compiler version we get some known
      # issues, such as warning #10237
      flag_ie = '{{}}intel-extensions{}' if system.is_windows else '{{}}{}intel-extensions'
      CFLAGS  += [flag_ie.format(flag_neg).format(flag_Q)]
      if not system.is_windows:
         LDFLAGS  += [flag_ie.format(flag_neg).format(flag_Q)]

   if env['MODE'] == 'opt':
      ## NOTE (#3305): We redefine these macros in order to not leak internal
      ## paths and functions. This will also raise compiler warnings #2011 (icc)
      ## and -Wbuiltin-macro-redefined (gcc)
      CPPDEFS += [{'__AI_FILE__'     : '\\"?\\"'}]
      CPPDEFS += [{'__AI_LINE__'     : '0'      }]
      CPPDEFS += [{'__AI_FUNCTION__' : '\\"?\\"'}]

   if env['MODE'] == 'debug':
      CPPDEFS += ['ARNOLD_DEBUG']

   if not env['ASSERTS']:
      CPPDEFS += ['NDEBUG']

   if env['USE_CLM']:
      CPPDEFS += ['AI_CONFIG_LICENSE_CLM']
   if env['USE_AZURE'] and not system.is_darwin:
      CPPDEFS += ['AI_CONFIG_LICENSE_AZURE']

   ## Sanitizers ###############################################################
   if env['SANITIZE']:
      # Supported sanitizers:
      # - address   : memory error detector        : http://clang.llvm.org/docs/AddressSanitizer.html
      # - leak      : memory leak detector         : http://clang.llvm.org/docs/LeakSanitizer.html
      # - memory    : uninitialized reads detector : http://clang.llvm.org/docs/MemorySanitizer.html
      # - undefined : undefined behavior detector  : http://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
      # - thread    : data races detector          : http://clang.llvm.org/docs/ThreadSanitizer.html

      sanitize_error_string = [
         'SANITIZE only allowed with COMPILER \'clang\' and \'gcc\'',
         'SANITIZE only allowed with MODE \'debug\'',
         'SANITIZE only allowed with MALLOC \'system\'',
      ]
      sanitize_error = None

      if compiler_name not in ['clang', 'gcc']: sanitize_error = 0
      elif env['MODE'] != 'debug'             : sanitize_error = 1
      elif env['MALLOC'] != 'system'          : sanitize_error = 2

      if sanitize_error is not None:
         raise SCons.Errors.UserError(sanitize_error_string[sanitize_error])

      # Enable the selected sanitizer
      flag_san = 'fsanitize={}'.format(env['SANITIZE'])
      CFLAGS  += [flag_san]
      LDFLAGS += [flag_san]

      # suppressions for the leak sanitizer
      def leak_supp_file():
         # Suppress reports from some external libraries
         lsan_supp = ['OpenColorIO::', 'OpenImageIO::', 'Imf::', 'Ptex*::', 'cineon::', 'dpx::']
         # Create a temporary file for storing all the suppressions
         f_lsan_supp = tempfile.NamedTemporaryFile(suffix='.supp', delete=False)
         f_lsan_supp.writelines('leak:{}\n'.format(item) for item in lsan_supp)
         f_lsan_supp.close()
         # return the temporal filename
         return f_lsan_supp.name

      # Environment variables for configuring every sanitizer at runtime
      sanitizer_options_env = {
         'address'  : 'ASAN_OPTIONS',
         'leak'     : 'LSAN_OPTIONS',
         'memory'   : 'MSAN_OPTIONS',
         'undefined': 'UBSAN_OPTIONS',
      }.get(env['SANITIZE'])

      # Runtime configuration for every sanitizer
      sanitizer_options = {
         'address': {
            'check_initialization_order' : 1,
            'detect_leaks'               : 0,
         },
         'leak': {
            'suppressions'               : leak_supp_file(),
            'print_suppressions'         : 0,
         },
         'undefined': {
            'print_stacktrace'           : 1,
         },
      }.get(env['SANITIZE'], {})

      # Setup the sanitizer runtime options
      if sanitizer_options_env:
         items  = list(sanitizer_options.items())
         string = ','.join('{}={}'.format(k, v) for k, v in items)
         env['ENV'][sanitizer_options_env] = string

   # Define some macros for configuring sanitazion in the code
   def attribute(x)  : return '__attribute__\(\({}\)\)'.format(x)
   def no_sanitize(x): return 'no_sanitize\(\\"{}\\"\)'.format(x)
   CPPDEFS += [{'__AI_ATTRIBUTE_NO_SANITIZE_ADDRESS__': attribute(no_sanitize('address')) if env['SANITIZE'] == 'address' else ''}]

   if env['CODE_COVERAGE']:
      # Enable Clang's source-based code coverage: It operates on AST and
      # preprocessor information directly. This allows it to generate very precise
      # coverage data
      #   http://releases.llvm.org/4.0.0/tools/clang/docs/SourceBasedCodeCoverage.html
      CFLAGS  += ['fprofile-instr-generate=arnold.profraw', 'fcoverage-mapping']
      LDFLAGS += ['fprofile-instr-generate=arnold.profraw', 'fcoverage-mapping']

   ## Append all the pre-processor / compiler / linker flags to the SCons
   ## construction environment. We first preprocess CFLAGS, CXXFLAGS and LDFLAGS
   ## by prefixing them with a leading '/' or '-', depending on the platform.
   def map_flag_prefix(flags, prefix):
      for i in range(len(flags)):
         flags[i] = env.Split(prefix + flags[i])

   map_flag_prefix(CFLAGS  , flag_prefix)
   map_flag_prefix(CXXFLAGS, flag_prefix)
   map_flag_prefix(LDFLAGS , flag_prefix)

   # Instead of using an env.Append() call for every list of flags (CFLAGS,
   # CXXFLAGS, etc.), we can use the collected dictionary for adding all of them
   # in a row with just one call.
   env.Append(**compiler_flags)
