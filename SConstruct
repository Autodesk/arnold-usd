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
import shutil
import sys, os
from SCons.Script import PathVariable
import SCons
from multiprocessing import cpu_count
# Disable warning about Python 2.6 being deprecated
SetOption('warn', 'no-python-version')

# Local helper tools
sys.path = [os.path.abspath(os.path.join('tools'))] + sys.path

from utils import system, configure
from utils.system import is_windows, is_linux, is_darwin
from utils.build_tools import *

# Allowed compilers
if is_windows:
    ALLOWED_COMPILERS = ['msvc', 'icc']
    arnold_default_api_lib = os.path.join('$ARNOLD_PATH', 'lib')
else:
    ALLOWED_COMPILERS = ['gcc', 'clang']
    arnold_default_api_lib = os.path.join('$ARNOLD_PATH', 'bin')

def compiler_validator(key, val, env):
    ## Convert the tuple of strings with allowed compilers and versions ...
    ##    ('clang:3.4.0', 'icc:14.0.2', 'gcc')
    ## ... into a proper dictionary ...
    ##    {'clang' : '3.4.0', 'icc' : '14.0.2', 'gcc' : None}
    allowed_versions  = {i[0] : i[1] if len(i) == 2 else None for i in (j.split(':') for j in ALLOWED_COMPILERS)}
    ## Parse the compiler format string <compiler>:<version>
    compiler = val.split(':')
    ## Validate if it's an allowed compiler
    if compiler[0] not in allowed_versions.keys():
        m = 'Invalid value for option {}: {}.  Valid values are: {}'
        raise SCons.Errors.UserError(m.format(key, val, allowed_versions.keys()))
    ## Set valid compiler name and version. If the version was not specified
    ## we will set the default version which we use in the official releases,
    ## so the build will fail if it cannot be found.
    env['_COMPILER'] = compiler[0]
    env['_COMPILER_VERSION'] = len(compiler) == 2 and compiler[1] or allowed_versions[compiler[0]]

# Scons doesn't provide a string variable
def StringVariable(key, help='', default=None, validator=None, converter=None):
    # We always get string values, so it's always valid and trivial to convert
    return (key, help, default, validator, converter)

# Custom variables definitions
vars = Variables('custom.py')
vars.AddVariables(
    PathVariable('BUILD_DIR', 'Directory where temporary build files are placed by scons', 'build', PathVariable.PathIsDirCreate),
    PathVariable('REFERENCE_DIR', 'Directory where the test reference images are stored.', 'testsuite', PathVariable.PathIsDirCreate),
    EnumVariable('MODE', 'Set compiler configuration', 'opt', allowed_values=('opt', 'debug', 'profile')),
    EnumVariable('WARN_LEVEL', 'Set warning level', 'warn-only', allowed_values=('strict', 'warn-only', 'none')),
    StringVariable('COMPILER', 'Set compiler to use', ALLOWED_COMPILERS[0], compiler_validator),
    PathVariable('SHCXX', 'C++ compiler used for generating shared-library objects', None),
    EnumVariable('CXX_STANDARD', 'C++ standard for gcc/clang.', '11', allowed_values=('11', '14', '17', '20')),
    BoolVariable('SHOW_CMDS', 'Display the actual command lines used for building', False),
    BoolVariable('COLOR_CMDS' , 'Display colored output messages when building', False),
    PathVariable('ARNOLD_PATH', 'Arnold installation root', os.getenv('ARNOLD_PATH', None), PathVariable.PathIsDir),
    PathVariable('ARNOLD_API_INCLUDES', 'Where to find Arnold API includes', os.path.join('$ARNOLD_PATH', 'include'), PathVariable.PathIsDir),
    PathVariable('ARNOLD_API_LIB', 'Where to find Arnold API static libraries', arnold_default_api_lib, PathVariable.PathIsDir),
    PathVariable('ARNOLD_BINARIES', 'Where to find Arnold API dynamic libraries and executables', os.path.join('$ARNOLD_PATH', 'bin'), PathVariable.PathIsDir),
    PathVariable('ARNOLD_PYTHON', 'Where to find Arnold python bindings', os.path.join('$ARNOLD_PATH', 'python'), PathVariable.PathIsDir),  
    PathVariable('USD_PATH', 'USD installation root', os.getenv('USD_PATH', None)),
    PathVariable('USD_INCLUDE', 'Where to find USD includes', os.path.join('$USD_PATH', 'include'), PathVariable.PathIsDir),
    PathVariable('USD_LIB', 'Where to find USD libraries', os.path.join('$USD_PATH', 'lib'), PathVariable.PathIsDir),
    StringVariable('USD_BIN', 'Where to find USD binaries', os.path.join('$USD_PATH', 'bin')),   
    EnumVariable('USD_BUILD_MODE', 'Build mode of USD libraries', 'monolithic', allowed_values=('shared_libs', 'monolithic', 'static')),
    StringVariable('USD_LIB_PREFIX', 'USD library prefix', 'lib'),
    BoolVariable('INSTALL_USD_PLUGIN_RESOURCES', 'Also install the content $USD_PATH/plugin/usd', False),
    # 'static'  will expect a static monolithic library "libusd_m". When doing a monolithic build of USD, this 
    # library can be found in the build/pxr folder
    PathVariable('BOOST_INCLUDE', 'Where to find Boost includes', '.', PathVariable.PathIsDir),
    PathVariable('BOOST_LIB', 'Where to find Boost libraries', '.', PathVariable.PathIsDir),
    BoolVariable('BOOST_ALL_NO_LIB', 'Disable automatic linking of boost libraries on Windows.', False),
    PathVariable('PYTHON_INCLUDE', 'Where to find Python includes (pyconfig.h)', os.getenv('PYTHON_INCLUDE', None)),
    PathVariable('PYTHON_LIB', 'Where to find Python libraries (python27.lib) ', os.getenv('PYTHON_LIB', None)),
    PathVariable('TBB_INCLUDE', 'Where to find TBB headers.', os.getenv('TBB_INCLUDE', None)),
    PathVariable('TBB_LIB', 'Where to find TBB libraries', os.getenv('TBB_LIB', None)),
    BoolVariable('TBB_STATIC', 'Whether we link against a static TBB library', False),
    StringVariable('EXTRA_STATIC_LIBS', 'Extra static libraries to link against when using usd static library', ''),
    # Google test dependency
    PathVariable('GOOGLETEST_PATH', 'Google Test installation root', '.', PathVariable.PathAccept),
    PathVariable('GOOGLETEST_INCLUDE', 'Where to find Google Test includes', os.path.join('$GOOGLETEST_PATH', 'include'), PathVariable.PathAccept),
    PathVariable('GOOGLETEST_LIB', 'Where to find Google Test libraries', os.path.join('$GOOGLETEST_PATH', 'lib64' if is_linux else 'lib'), PathVariable.PathAccept),
    BoolVariable('ENABLE_UNIT_TESTS', 'Whether or not to enable C++ unit tests. This feature requires Google Test.', False),
    EnumVariable('TEST_ORDER', 'Set the execution order of tests to be run', 'reverse', allowed_values=('normal', 'reverse')),
    EnumVariable('SHOW_TEST_OUTPUT', 'Display the test log as it is being run', 'single', allowed_values=('always', 'never', 'single')),
    EnumVariable('USE_VALGRIND', 'Enable Valgrinding', 'False', allowed_values=('False', 'True', 'Full')),
    BoolVariable('UPDATE_REFERENCE', 'Update the reference log/image for the specified targets', False),
    BoolVariable('UPDATE_HYDRA_TESTS_GROUP', 'Add the new tests passing to the hydra group', False),
    PathVariable('PREFIX', 'Directory to install under', '.', PathVariable.PathIsDirCreate),
    PathVariable('PREFIX_PROCEDURAL', 'Directory to install the procedural under.', os.path.join('$PREFIX', 'procedural'), PathVariable.PathIsDirCreate),
    PathVariable('PREFIX_RENDER_DELEGATE', 'Directory to install the render delegate under.', os.path.join('$PREFIX', 'plugin'), PathVariable.PathIsDirCreate),
    PathVariable('PREFIX_NDR_PLUGIN', 'Directory to install the ndr plugin under.', os.path.join('$PREFIX', 'plugin'), PathVariable.PathIsDirCreate),
    PathVariable('PREFIX_USD_IMAGING_PLUGIN', 'Directory to install the usd imaging plugin under.', os.path.join('$PREFIX', 'plugin'), PathVariable.PathIsDirCreate),
    PathVariable('PREFIX_SCENE_DELEGATE', 'Directory to install the scene delegate under.', os.path.join('$PREFIX', 'plugin'), PathVariable.PathIsDirCreate),
    PathVariable('PREFIX_HEADERS', 'Directory to install the headers under.', os.path.join('$PREFIX', 'include'), PathVariable.PathIsDirCreate),
    PathVariable('PREFIX_SCHEMAS', 'Directory to install the schemas under.', os.path.join('$PREFIX', 'schema'), PathVariable.PathIsDirCreate),
    PathVariable('PREFIX_BIN', 'Directory to install the binaries under.', os.path.join('$PREFIX', 'bin'), PathVariable.PathIsDirCreate),
    PathVariable('PREFIX_DOCS', 'Directory to install the documentation under.', os.path.join('$PREFIX', 'docs'), PathVariable.PathIsDirCreate),
    BoolVariable('SHOW_PLOTS', 'Display timing plots for the testsuite. gnuplot has to be found in the environment path.', False),
    BoolVariable('BUILD_SCHEMAS', 'Whether or not to build the schemas and their wrapper.', True),
    BoolVariable('BUILD_RENDER_DELEGATE', 'Whether or not to build the hydra render delegate.', True),
    BoolVariable('BUILD_NDR_PLUGIN', 'Whether or not to build the node registry plugin.', True),
    BoolVariable('BUILD_USD_IMAGING_PLUGIN', 'Whether or not to build the usdImaging plugin.', True),
    BoolVariable('BUILD_PROCEDURAL', 'Whether or not to build the arnold procedural.', True),
    BoolVariable('BUILD_SCENE_DELEGATE', 'Whether or not to build the arnold scene delegate.', False),
    BoolVariable('BUILD_TESTSUITE', 'Whether or not to build the testsuite.', True),
    BoolVariable('BUILD_DOCS', 'Whether or not to build the documentation.', True),
    BoolVariable('PROC_SCENE_FORMAT', 'Whether or not to build the procedural with a scene format plugin.', True),
    BoolVariable('DISABLE_CXX11_ABI', 'Disable the use of the CXX11 abi for gcc/clang', False),
    BoolVariable('ENABLE_HYDRA_IN_USD_PROCEDURAL', 'Enable building hydra render delegate in the usd procedural', False),
    BoolVariable('BUILD_USDGENSCHEMA_ARNOLD', 'Whether or not to build the simplified usdgenschema', False),
    BoolVariable('IGNORE_ARCH_FLAGS', 'Ignore the arch flags when compiling usdgenschema', False),
    StringVariable('BOOST_LIB_NAME', 'Boost library name pattern', 'boost_%s'),
    StringVariable('TBB_LIB_NAME', 'TBB library name pattern', '%s'),
    StringVariable('USD_MONOLITHIC_LIBRARY', 'Name of the USD monolithic library', 'usd_ms'),
    StringVariable('PYTHON_LIB_NAME', 'Name of the python library', 'python27'),
    StringVariable('USD_PROCEDURAL_NAME', 'Name of the usd procedural.', 'usd'),
    StringVariable('USDGENSCHEMA_CMD', 'Custom command to run usdGenSchema', None),
    StringVariable('TESTSUITE_OUTPUT', 'Optional output path where the testsuite results are saved', None),
    StringVariable('JUNIT_TESTSUITE_NAME', 'Optional name for the JUnit report', None),
    StringVariable('JUNIT_TESTSUITE_URL', 'Optional URL for the JUnit report', None),
    BoolVariable('REPORT_ONLY_FAILED_TESTS', 'Only failed test will be kept', False),
    StringVariable('TIMELIMIT', 'Time limit for each test (in seconds)', '300'),
    ('TEST_PATTERN', 'Glob pattern of tests to be run', 'test_*'),
    ('KICK_PARAMS', 'Additional parameters for kick', '-v 6'),
    ('TESTSUITE_RERUNS_FAILED', 'Numbers of reruns of failed test to detect instability', 0),
    ('TESTSUITE_INSTABILITY_THRESHOLD', 'Make the testsuite fail if the unstable test count is above the threshold percentage of the total test count', 0.1)
)

if is_windows:
    vars.Add(('MSVC_VERSION', 'Version of MS Visual C++ Compiler to use', '14.2'))
    # If explicitely provided in the command line, MSVC_USE_SCRIPT will override the MSVC_VERSION.
    # MSVC_USE_SCRIPT should point to a "vcvars*.bat" script
    if 'MSVC_USE_SCRIPT' in ARGUMENTS:
        vars.Add(('MSVC_USE_SCRIPT', 'Script which overrides the MSVC_VERSION detection', ARGUMENTS.get('MSVC_USE_SCRIPT')))
else:
    vars.Add(BoolVariable('RPATH_ADD_ARNOLD_BINARIES', 'Add Arnold binaries to the RPATH', False))

if is_darwin:
    vars.Add(('SDK_VERSION', 'Version of the Mac OSX SDK to use', '')) # use system default
    vars.Add(PathVariable('SDK_PATH', 'Root path to installed OSX SDKs', '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs'))
    vars.Add(('MACOS_VERSION_MIN', 'Minimum compatibility with Mac OSX', '10.11'))
    vars.Add(('MACOS_ARCH', 'Mac OS ARCH', 'x86_64'))
    vars.Add(StringVariable('EXTRA_FRAMEWORKS', 'Optional frameworks to link against. semi colon separated list of framework names', ''))


# Create the scons environment
env = Environment(variables = vars, ENV = os.environ, tools = ['default'])

BUILD_DIR = env.subst(env['BUILD_DIR'])
REFERENCE_DIR = env.subst(env['REFERENCE_DIR'])

## Tells SCons to store all file signatures in the database
## ".sconsign.<SCons_version>.dblite" instead of the default ".sconsign.dblite".
SConsignFile(os.path.join(BUILD_DIR, '.sconsign.%s' % (SCons.__version__)))

# We are disabling unit tests on MacOS for now.
if is_darwin:
    env['ENABLE_UNIT_TESTS'] = False

env['ARNOLD_ADP_DISABLE'] = "1"
os.environ['ARNOLD_ADP_DISABLE'] = '1'

def get_optional_env_path(env_name):
    return os.path.abspath(env.subst(env[env_name])) if env_name in env else None

USD_BUILD_MODE        = env['USD_BUILD_MODE']

BUILD_USDGENSCHEMA_ARNOLD    = env['BUILD_USDGENSCHEMA_ARNOLD']
BUILD_RENDER_DELEGATE        = env['BUILD_RENDER_DELEGATE'] if USD_BUILD_MODE != 'static' else False
BUILD_SCENE_DELEGATE         = env['BUILD_SCENE_DELEGATE'] if USD_BUILD_MODE != 'static' else False
BUILD_PROCEDURAL             = env['BUILD_PROCEDURAL']
BUILD_TESTSUITE              = env['BUILD_TESTSUITE']
BUILD_DOCS                   = env['BUILD_DOCS']

USD_LIB_PREFIX        = env['USD_LIB_PREFIX']

# if we want the hydra procedural to be enabled with a static USD, we need
# schemas, usd_imaging and ndr plugins to be compiled as well. For shared / monolithic USD builds
# we might want these modules to be built separately
if BUILD_PROCEDURAL and env['ENABLE_HYDRA_IN_USD_PROCEDURAL'] and USD_BUILD_MODE == 'static':
    env['BUILD_SCHEMAS'] = True
    env['BUILD_USD_IMAGING_PLUGIN'] = True
    env['BUILD_NDR_PLUGIN'] = True

BUILD_SCHEMAS                = env['BUILD_SCHEMAS']
BUILD_NDR_PLUGIN             = env['BUILD_NDR_PLUGIN']
BUILD_USD_IMAGING_PLUGIN     = env['BUILD_USD_IMAGING_PLUGIN'] if BUILD_SCHEMAS else False

# Set default amount of threads set to the cpu counts in this machine.
# This can be overridden through command line by setting e.g. "abuild -j 1"
SetOption('num_jobs', int(cpu_count()))

env['USD_LIB_AS_SOURCE'] = None
# There are two possible behaviors with USD_LIB_PREFIX, if it starts with 'lib'
# then we have to remove it, since gcc and clang automatically substitutes it on
# non windows platforms. If the prefix does not start with lib, then we have to
# force scons to properly link against libs named in a non-standard way.
if not is_windows:
    if USD_LIB_PREFIX.startswith('lib'):
        USD_LIB_PREFIX = USD_LIB_PREFIX[3:]
    else:
        # Scons needs this variable, so we can pass uniquely named shared
        # objects to link against. This is only required on osx and linux.
        env['STATIC_AND_SHARED_OBJECTS_ARE_THE_SAME'] = 1
        env['USD_LIB_AS_SOURCE'] = True

env['USD_LIB_PREFIX'] = USD_LIB_PREFIX

ARNOLD_PATH         = os.path.abspath(env.subst(env['ARNOLD_PATH']))
ARNOLD_API_INCLUDES = os.path.abspath(env.subst(env['ARNOLD_API_INCLUDES']))
ARNOLD_API_LIB      = os.path.abspath(env.subst(env['ARNOLD_API_LIB']))
ARNOLD_BINARIES     = os.path.abspath(env.subst(env['ARNOLD_BINARIES']))


if not is_windows and env['RPATH_ADD_ARNOLD_BINARIES']:
    env['RPATH'] = ARNOLD_BINARIES

env['ARNOLD_BINARIES'] = ARNOLD_BINARIES

PREFIX                    = env.subst(env['PREFIX'])
PREFIX_PROCEDURAL         = env.subst(env['PREFIX_PROCEDURAL'])
PREFIX_RENDER_DELEGATE    = env.subst(env['PREFIX_RENDER_DELEGATE'])
PREFIX_NDR_PLUGIN         = env.subst(env['PREFIX_NDR_PLUGIN'])
PREFIX_USD_IMAGING_PLUGIN = env.subst(env['PREFIX_USD_IMAGING_PLUGIN'])
PREFIX_SCENE_DELEGATE     = env.subst(env['PREFIX_SCENE_DELEGATE'])
PREFIX_HEADERS            = env.subst(env['PREFIX_HEADERS'])
PREFIX_SCHEMAS            = env.subst(env['PREFIX_SCHEMAS'])
PREFIX_BIN                = env.subst(env['PREFIX_BIN'])
PREFIX_DOCS               = env.subst(env['PREFIX_DOCS'])

USD_PATH = os.path.abspath(env.subst(env['USD_PATH']))
USD_INCLUDE = os.path.abspath(env.subst(env['USD_INCLUDE']))
USD_LIB = os.path.abspath(env.subst(env['USD_LIB']))
USD_BIN = os.path.abspath(env.subst(env['USD_BIN']))

# Storing values after expansion
env['USD_PATH'] = USD_PATH
env['USD_INCLUDE'] = USD_INCLUDE
env['USD_LIB'] = USD_LIB
env['USD_BIN'] = USD_BIN
env['PREFIX_RENDER_DELEGATE'] = PREFIX_RENDER_DELEGATE

# these could be supplied by linux / osx
BOOST_INCLUDE = get_optional_env_path('BOOST_INCLUDE')
BOOST_LIB = get_optional_env_path('BOOST_LIB')
PYTHON_INCLUDE = get_optional_env_path('PYTHON_INCLUDE')
PYTHON_LIB = get_optional_env_path('PYTHON_LIB')
TBB_INCLUDE = get_optional_env_path('TBB_INCLUDE')
TBB_LIB = get_optional_env_path('TBB_LIB')
if env['ENABLE_UNIT_TESTS']:
    GOOGLETEST_INCLUDE = env.subst(env['GOOGLETEST_INCLUDE'])
    GOOGLETEST_LIB = env.subst(env['GOOGLETEST_LIB'])
else:
    GOOGLETEST_INCLUDE = None
    GOOGLETEST_LIB = None

env['PYTHON_LIBRARY'] = File(env['PYTHON_LIB_NAME']) if os.path.isabs(env['PYTHON_LIB_NAME']) else env['PYTHON_LIB_NAME']

if env['_COMPILER'] == 'clang':
   env.Tool('clang', version=env['_COMPILER_VERSION'])

# force compiler to match SHCXX
if env['SHCXX'] != '$CXX':
   env['CXX'] = env['SHCXX']

# Get Arnold version
env['ARNOLD_VERSION'] = get_arnold_version(ARNOLD_API_INCLUDES)

if env['PROC_SCENE_FORMAT']:
    env['ARNOLD_HAS_SCENE_FORMAT_API'] = get_arnold_has_scene_format_api(ARNOLD_API_INCLUDES)
else:
    env['ARNOLD_HAS_SCENE_FORMAT_API'] = 0
    
if BUILD_SCHEMAS or BUILD_RENDER_DELEGATE or BUILD_NDR_PLUGIN or BUILD_USD_IMAGING_PLUGIN or BUILD_SCENE_DELEGATE or BUILD_PROCEDURAL or BUILD_DOCS:
    # Get USD Version
    header_info = get_usd_header_info(USD_INCLUDE) 
    env['USD_VERSION'] = header_info['USD_VERSION']
    env['USD_VERSION_INT'] = header_info['USD_VERSION_INT']
    env['USD_HAS_PYTHON_SUPPORT'] = header_info['USD_HAS_PYTHON_SUPPORT']
    env['USD_HAS_UPDATED_COMPOSITOR'] = header_info['USD_HAS_UPDATED_COMPOSITOR']
    env['USD_HAS_FULLSCREEN_SHADER'] = header_info['USD_HAS_FULLSCREEN_SHADER']
elif BUILD_TESTSUITE:
    # Need to set dummy values for the testsuite to run properly without 
    # recompiling arnold-usd
    env['USD_VERSION'] = ''
    env['USD_HAS_PYTHON_SUPPORT'] = ''

if env['_COMPILER'] in ['gcc', 'clang'] and env['SHCXX'] != '$CXX':
   env['GCC_VERSION'] = os.path.splitext(os.popen(env['SHCXX'] + ' -dumpversion').read())[0]

print("Building Arnold-USD:")
print(" - Build mode: '{}'".format(env['MODE']))
print(" - Host OS: '{}'".format(system.os))
print(" - Arnold version: '{}'".format(env['ARNOLD_VERSION']))
#print(" - Environment:")
#for k, v in os.environ.items():
#    print("     {} = {}".format(k,v))


# Platform definitions
if is_darwin:
    env.Append(CPPDEFINES = Split('_DARWIN'))
elif is_linux:
    env.Append(CPPDEFINES = Split('_LINUX'))
elif is_windows:
    env.Append(CPPDEFINES = Split('_WINDOWS _WIN32 WIN32 _USE_MATH_DEFINES'))
    env.Append(CPPDEFINES = Split('_WIN64'))
    env.Append(LINKFLAGS=Split('/DEBUG'))
    if env['TBB_LIB_NAME'] != '%s':
        env.Append(CPPDEFINES = Split('__TBB_NO_IMPLICIT_LINKAGE=1'))
    if env['BOOST_ALL_NO_LIB']:
        env.Append(CPPDEFINES = Split('BOOST_ALL_NO_LIB HBOOST_ALL_NO_LIB'))

# This definition allows to re-enable deprecated function when using c++17 headers, this fixes the compilation issue
#   error: no template named 'unary_function' in namespace 'std'
if env['_COMPILER'] == 'clang':
    env.Append(CPPDEFINES = Split('_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION'))

# If USD is built in static, we need to define PXR_STATIC in order to hide the symbols
if env['USD_BUILD_MODE'] == 'static':
    env.Append(CPPDEFINES=['PXR_STATIC'])

# Adding USD paths to environment for the teststuite
dylib = 'PATH' if is_windows else ('DYLD_LIBRARY_PATH' if is_darwin else 'LD_LIBRARY_PATH')
env_separator = ';' if is_windows else ':'

env.AppendENVPath(dylib, USD_LIB, envname='ENV', sep=env_separator, delete_existing=1)
env.AppendENVPath(dylib, USD_BIN, envname='ENV', sep=env_separator, delete_existing=1)
env.AppendENVPath(dylib, ARNOLD_BINARIES, envname='ENV', sep=env_separator, delete_existing=1)
env.AppendENVPath('PYTHONPATH', os.path.join(USD_LIB, 'python'), envname='ENV', sep=env_separator, delete_existing=1)
env.AppendENVPath('PXR_PLUGINPATH_NAME', os.path.join(USD_PATH, 'plugin', 'usd'), envname='ENV', sep=env_separator, delete_existing=1)
os.environ['PATH'] = env['ENV']['PATH']
os.putenv('PATH', os.environ['PATH'])
os.environ['PYTHONPATH'] = env['ENV']['PYTHONPATH']
os.putenv('PYTHONPATH', os.environ['PYTHONPATH'])
os.environ['PXR_PLUGINPATH_NAME'] = env['ENV']['PXR_PLUGINPATH_NAME']
os.putenv('PXR_PLUGINPATH_NAME', os.environ['PXR_PLUGINPATH_NAME'])

env['ENV']['ARNOLD_PATH'] = os.path.abspath(ARNOLD_PATH)
env['ENV']['ARNOLD_BINARIES'] = os.path.abspath(ARNOLD_BINARIES)
env['ENV']['PREFIX_BIN'] = os.path.abspath(PREFIX_BIN)
env['ENV']['PREFIX_PROCEDURAL'] = os.path.abspath(PREFIX_PROCEDURAL)

# Compiler settings
if env['_COMPILER'] in ['gcc', 'clang']:
    env.Append(CCFLAGS = Split('-fno-operator-names -std=c++{}'.format(env['CXX_STANDARD'])))
    if is_darwin:
        env_dict = env.Dictionary()
        # Minimum compatibility with Mac OSX "env['MACOS_VERSION_MIN']"
        env.Append(CCFLAGS   = ['-mmacosx-version-min={MACOS_VERSION_MIN}'.format(**env_dict)])
        env.Append(LINKFLAGS = ['-mmacosx-version-min={MACOS_VERSION_MIN}'.format(**env_dict)])
        env.Append(CCFLAGS   = ['-isysroot','{SDK_PATH}/MacOSX{SDK_VERSION}.sdk/'.format(**env_dict)])
        env.Append(LINKFLAGS = ['-isysroot','{SDK_PATH}/MacOSX{SDK_VERSION}.sdk/'.format(**env_dict)])
        env.Append(CXXFLAGS  = ['-stdlib=libc++'])
        env.Append(LINKFLAGS = ['-stdlib=libc++'])
        if env['MACOS_ARCH'] == 'x86_64':
            env.Append(CCFLAGS   = ['-arch', 'x86_64'])
            env.Append(LINKFLAGS = ['-arch', 'x86_64'])
        if env['MACOS_ARCH'] == 'arm64':
            env.Append(CCFLAGS   = ['-arch', 'arm64'])
            env.Append(LINKFLAGS = ['-arch', 'arm64'])
        if env['MACOS_ARCH'].find('arm64') >= 0 and env['MACOS_ARCH'].find('x86_64') >= 0:
            env.Append(CCFLAGS   = ['-arch', 'arm64'])
            env.Append(CCFLAGS   = ['-arch', 'x86_64'])
            env.Append(LINKFLAGS = ['-arch', 'arm64'])
            env.Append(LINKFLAGS = ['-arch', 'x86_64'])
    else:
        env.Append(LINKFLAGS = '-Wl,--no-undefined')
        if env['DISABLE_CXX11_ABI']:
            env.Append(CPPDEFINES = [{'_GLIBCXX_USE_CXX11_ABI' : 0}])
    # Warning level
    if env['WARN_LEVEL'] == 'none':
        env.Append(CCFLAGS = Split('-w'))
    else:
        env.Append(CCFLAGS = Split('-Wall -Wsign-compare -Wno-deprecated-register -Wno-undefined-var-template -Wno-unused-local-typedef'))
        if env['WARN_LEVEL'] == 'strict':
            env.Append(CCFLAGS = Split('-Werror'))

    # Optimization flags
    if env['MODE'] == 'opt' or env['MODE'] == 'profile':
        env.Append(CCFLAGS = Split('-O3'))

    # Debug and profile flags
    if env['MODE'] == 'debug' or env['MODE'] == 'profile':
        env.ParseFlags('-DDEBUG')
        env.Append(CCFLAGS = Split('-g'))
        env.Append(LINKFLAGS = Split('-g'))
        env.Append(CCFLAGS = Split('-O0'))

    # Linux profiling
    if system.os == 'linux' and env['MODE'] == 'profile':
        env.Append(CCFLAGS = Split('-pg'))
        env.Append(LINKFLAGS = Split('-pg'))

# msvc settings
elif env['_COMPILER'] == 'msvc':
    cxx_standard = env['CXX_STANDARD'];
    if cxx_standard != '11':
        env.Append(CCFLAGS=Split('/std:c++{}'.format(cxx_standard)))
    env.Append(CCFLAGS=Split('/EHsc'))
    # Removes "warning C4003: not enough arguments for function-like macro invocation 'BOOST_PP_SEQ_DETAIL_EMPTY_SIZE'"
    # introduced with usd 23.11
    env.Append(CCFLAGS=Split('/wd4003'))

    env.Append(LINKFLAGS=Split('/Machine:X64'))
    # Ignore all the linking warnings we get on windows, coming from USD
    env.Append(LINKFLAGS=Split('/ignore:4099'))
    env.Append(LINKFLAGS=Split('/ignore:4217'))

    env.Append(CCFLAGS=Split('/D "NOMINMAX" /D "TBB_SUPPRESS_DEPRECATED_MESSAGES" /Zc:inline-'))
    # Optimization/profile/debug flags
    if env['MODE'] == 'opt':
        env.Append(CCFLAGS=Split('/O2 /Oi /Ob2 /MD'))
        env.Append(CPPDEFINES=Split('NDEBUG'))
    elif env['MODE'] == 'profile':
        env.Append(CCFLAGS=Split('/Ob2 /MD /Zi'))
    else:  # debug mode
        env.Append(CCFLAGS=Split('/Od /Zi /MD'))
        env.Append(LINKFLAGS=Split('/DEBUG'))


if not env['SHOW_CMDS']:
    # Hide long compile lines from the user
    arch = env['MACOS_ARCH'] if is_darwin else 'x86_64'
    env['CCCOMSTR']     = 'Compiling {} $SOURCE ...'.format(arch)
    env['SHCCCOMSTR']   = 'Compiling {} $SOURCE ...'.format(arch)
    env['CXXCOMSTR']    = 'Compiling {} $SOURCE ...'.format(arch)
    env['SHCXXCOMSTR']  = 'Compiling {} $SOURCE ...'.format(arch)
    env['LINKCOMSTR']   = 'Linking {} $TARGET ...'.format(arch)
    env['SHLINKCOMSTR'] = 'Linking {} $TARGET ...'.format(arch)
    env['LEXCOMSTR']    = 'Generating $TARGET ...'
    env['YACCCOMSTR']   = 'Generating $TARGET ...'
    env['RCCOMSTR']     = 'Generating $TARGET ...'
    if env['COLOR_CMDS']:
        from tools.contrib import colorama
        from tools.contrib.colorama import Fore, Style
        colorama.init(convert=system.is_windows, strip=False)

        ansi_bold_green     = Fore.GREEN + Style.BRIGHT
        ansi_bold_red       = Fore.RED + Style.BRIGHT
        ansi_bold_yellow    = Fore.YELLOW + Style.BRIGHT

        env['CCCOMSTR']     = ansi_bold_green + env['CCCOMSTR'] + Style.RESET_ALL
        env['SHCCCOMSTR']   = ansi_bold_green + env['SHCCCOMSTR'] + Style.RESET_ALL
        env['CXXCOMSTR']    = ansi_bold_green + env['CXXCOMSTR'] + Style.RESET_ALL
        env['SHCXXCOMSTR']  = ansi_bold_green + env['SHCXXCOMSTR'] + Style.RESET_ALL
        env['LINKCOMSTR']   = ansi_bold_red + env['LINKCOMSTR'] + Style.RESET_ALL
        env['SHLINKCOMSTR'] = ansi_bold_red + env['SHLINKCOMSTR'] + Style.RESET_ALL
        env['LEXCOMSTR']    = ansi_bold_yellow + env['LEXCOMSTR'] + Style.RESET_ALL
        env['YACCCOMSTR']   = ansi_bold_yellow + env['YACCCOMSTR'] + Style.RESET_ALL
        env['RCCOMSTR']     = ansi_bold_yellow + env['RCCOMSTR'] + Style.RESET_ALL

# Add include and lib paths to Arnold
env.Append(CPPPATH = [ARNOLD_API_INCLUDES, USD_INCLUDE])
env.Append(LIBPATH = [ARNOLD_API_LIB, ARNOLD_BINARIES, USD_LIB])

# Add optional include and library paths. These are the standard additional
# libraries required when using USD.
env.Append(CPPPATH = [p for p in [BOOST_INCLUDE, PYTHON_INCLUDE, TBB_INCLUDE, GOOGLETEST_INCLUDE] if p is not None])
env.Append(LIBPATH = [p for p in [BOOST_LIB, PYTHON_LIB, TBB_LIB, GOOGLETEST_LIB] if p is not None])

env['ROOT_DIR'] = os.getcwd()


# Configure base directory for temp files
if is_darwin:
    BUILD_BASE_DIR = os.path.join(BUILD_DIR, '%s_%s' % (system.os, env['MACOS_ARCH']), '%s_%s' % (env['_COMPILER'], env['MODE']), 'usd-%s_arnold-%s' % (env['USD_VERSION'], env['ARNOLD_VERSION']))
else:
    BUILD_BASE_DIR = os.path.join(BUILD_DIR, '%s_%s' % (system.os, 'x86_64'), '%s_%s' % (env['_COMPILER'], env['MODE']), 'usd-%s_arnold-%s' % (env['USD_VERSION'], env['ARNOLD_VERSION']))

env['BUILD_BASE_DIR'] = BUILD_BASE_DIR
# Build target
if os.path.isabs(BUILD_BASE_DIR):
    env['BUILD_ROOT_DIR'] = BUILD_BASE_DIR
else:
    env['BUILD_ROOT_DIR'] = os.path.join(env['ROOT_DIR'], BUILD_BASE_DIR)

if os.path.isabs(REFERENCE_DIR):
    env['REFERENCE_DIR_ROOT'] = REFERENCE_DIR
else:
    env['REFERENCE_DIR_ROOT'] = os.path.join(env['ROOT_DIR'], REFERENCE_DIR)

# Propagate any "library path" environment variable to scons
# if system.os == 'linux':
#     add_to_library_path(env, '.')
#     if os.environ.has_key('LD_LIBRARY_PATH'):
#         add_to_library_path(env, os.environ['LD_LIBRARY_PATH'])
#     os.environ['LD_LIBRARY_PATH'] = env['ENV']['LD_LIBRARY_PATH']
# elif system.os == 'darwin':
#     if os.environ.has_key('DYLD_LIBRARY_PATH'):
#         add_to_library_path(env, os.environ['DYLD_LIBRARY_PATH'])
# elif system.os == 'windows':
#     add_to_library_path(env, os.environ['PATH'])
#     os.environ['PATH'] = env['ENV']['PATH']

#
# SCons scripts to build
#
usdgenschema_script = os.path.join('tools', 'usdgenschema', 'SConscript')
usdgenschema_build = os.path.join(BUILD_BASE_DIR, 'usdgenschema')

# common 
env.Append(CPPPATH = [os.path.join(env['ROOT_DIR'], 'libs', 'common')])
#env['COMMON_SRC'] = [os.path.join(env['ROOT_DIR'], 'libs', 'common', src) for src in find_files_recursive(os.path.join(env['ROOT_DIR'], 'libs', 'common'), ['.cpp'])]
common_script = os.path.join('libs', 'common', 'SConscript')
common_build = os.path.join(BUILD_BASE_DIR, 'libs', 'common')
COMMON = env.SConscript(common_script, variant_dir = common_build, duplicate = 0, exports = 'env')

procedural_script = os.path.join('plugins', 'procedural', 'SConscript')
procedural_build = os.path.join(BUILD_BASE_DIR, 'plugins', 'procedural')

schemas_script = os.path.join('schemas', 'SConscript')
schemas_build = os.path.join(BUILD_BASE_DIR, 'schemas')

translator_script = os.path.join('libs', 'translator', 'SConscript')
translator_build = os.path.join(BUILD_BASE_DIR, 'libs', 'translator')

renderdelegate_script = os.path.join('libs', 'render_delegate', 'SConscript')
renderdelegate_build = os.path.join(BUILD_BASE_DIR, 'libs', 'render_delegate')

renderdelegateplugin_script = os.path.join('plugins', 'render_delegate', 'SConscript')
renderdelegateplugin_build = os.path.join(BUILD_BASE_DIR, 'plugins', 'render_delegate')
renderdelegateplugin_plug_info = os.path.join('plugins', 'render_delegate', 'plugInfo.json.in')
renderdelegateplugin_out_plug_info = os.path.join(renderdelegateplugin_build, 'plugInfo.json')

ndrplugin_script = os.path.join('plugins', 'ndr', 'SConscript')
ndrplugin_build = os.path.join(BUILD_BASE_DIR, 'plugins', 'ndr')
ndrplugin_plug_info = os.path.join('plugins', 'ndr', 'plugInfo.json.in')
ndrplugin_out_plug_info = os.path.join(ndrplugin_build, 'plugInfo.json')

usdimagingplugin_script = os.path.join('plugins', 'usd_imaging', 'SConscript')
usdimagingplugin_build = os.path.join(BUILD_BASE_DIR, 'plugins', 'usd_imaging')
usdimagingplugin_plug_info = os.path.join('plugins', 'usd_imaging', 'plugInfo.json.in')
usdimagingplugin_out_plug_info = os.path.join(usdimagingplugin_build, 'plugInfo.json')

scenedelegate_script = os.path.join('plugins', 'scene_delegate', 'SConscript')
scenedelegate_build = os.path.join(BUILD_BASE_DIR, 'plugins', 'scene_delegate')
scenedelegate_plug_info = os.path.join('plugins', 'scene_delegate', 'plugInfo.json.in')
scenedelegate_out_plug_info = os.path.join(scenedelegate_build, 'plugInfo.json')

testsuite_build = env.get('TESTSUITE_OUTPUT') or os.path.join(BUILD_BASE_DIR, 'testsuite')

if (BUILD_PROCEDURAL and env['ENABLE_HYDRA_IN_USD_PROCEDURAL']) or BUILD_RENDER_DELEGATE: # This could be disabled adding an experimental mode
    RENDERDELEGATE = env.SConscript(renderdelegate_script, variant_dir = renderdelegate_build, duplicate = 0, exports = 'env') 
else:
    RENDERDELEGATE = None

# Define targets
# Target for the USD procedural

if BUILD_PROCEDURAL:
    TRANSLATOR = env.SConscript(translator_script,
        variant_dir = translator_build,
        duplicate = 0, exports = 'env')

    SConscriptChdir(0)
else:
    TRANSLATOR = None

# Define targets

if BUILD_USDGENSCHEMA_ARNOLD:
    USDGENSCHEMA_ARNOLD = env.SConscript(usdgenschema_script, variant_dir = usdgenschema_build, duplicate = 0, exports = 'env')
    SConscriptChdir(0)
    # Override the usdgenschema command with our command
    env['USDGENSCHEMA_CMD'] = USDGENSCHEMA_ARNOLD[0]
    # Also copy the usd resource folder
    usd_input_resource_folders = [os.path.join(USD_LIB, 'usd'), os.path.join(procedural_build, 'usd')]
    usd_target_resource_folder = os.path.join(os.path.dirname(str(env['USDGENSCHEMA_CMD'])), "usd")
    for usd_input_resource_folder in usd_input_resource_folders:
        if os.path.exists(usd_input_resource_folder):
            for entry in os.listdir(usd_input_resource_folder):
                source_dir = os.path.join(usd_input_resource_folder, entry)
                target_dir = os.path.join(usd_target_resource_folder, entry)
                if os.path.isdir(source_dir) and not os.path.exists(target_dir):
                    shutil.copytree(source_dir, target_dir)
                # Also copy the plugInfo.
    shutil.copy2(os.path.join(USD_LIB, 'usd', 'plugInfo.json'), usd_target_resource_folder)
else: 
    USDGENSCHEMA_ARNOLD = None


if BUILD_SCHEMAS:
    SCHEMAS = env.SConscript(schemas_script,
        variant_dir = schemas_build,
        duplicate = 0, exports = 'env')
    SConscriptChdir(0)
    if USDGENSCHEMA_ARNOLD:
        Depends(SCHEMAS, USDGENSCHEMA_ARNOLD[0])      
else:
    SCHEMAS = None

if BUILD_RENDER_DELEGATE:
    RENDERDELEGATEPLUGIN = env.SConscript(renderdelegateplugin_script, variant_dir = renderdelegateplugin_build, duplicate = 0, exports = 'env')
    Depends(RENDERDELEGATEPLUGIN, COMMON[0])
    SConscriptChdir(0)
else:
    RENDERDELEGATEPLUGIN = None

if BUILD_NDR_PLUGIN:
    NDRPLUGIN = env.SConscript(ndrplugin_script, variant_dir = ndrplugin_build, duplicate = 0, exports = 'env')
    Depends(NDRPLUGIN, COMMON[0])
    SConscriptChdir(0)
else:
    NDRPLUGIN = None

if BUILD_USD_IMAGING_PLUGIN:
    USDIMAGINGPLUGIN = env.SConscript(usdimagingplugin_script, variant_dir = usdimagingplugin_build, duplicate = 0, exports = 'env')
    Depends(USDIMAGINGPLUGIN, COMMON[0])
    SConscriptChdir(0)
else:
    USDIMAGINGPLUGIN = None

if BUILD_SCENE_DELEGATE:
    SCENEDELEGATE = env.SConscript(scenedelegate_script, variant_dir = scenedelegate_build, duplicate = 0, exports = 'env')
    Depends(SCENEDELEGATE, COMMON[0])
    SConscriptChdir(0)
else:
    SCENEDELEGATE = None


# Target for the USD procedural
if BUILD_PROCEDURAL:
    PROCEDURAL = env.SConscript(procedural_script,
        variant_dir = procedural_build,
        duplicate = 0, exports = 'env')
    SConscriptChdir(0)
    Depends(PROCEDURAL, TRANSLATOR[0])
    Depends(PROCEDURAL, COMMON[0])
    if env['ENABLE_HYDRA_IN_USD_PROCEDURAL']:
        Depends(PROCEDURAL, RENDERDELEGATE[0])
        if BUILD_NDR_PLUGIN:
            Depends(PROCEDURAL, NDRPLUGIN[0])
        if BUILD_USD_IMAGING_PLUGIN:            
            Depends(PROCEDURAL, USDIMAGINGPLUGIN[0])
        if BUILD_SCHEMAS:
            Depends(PROCEDURAL, SCHEMAS[0])

    if env['USD_BUILD_MODE'] == 'static':
        # For static builds of the procedural, we need to copy the usd 
        # resources to the same path as the procedural
        usd_target_resource_folder = os.path.join(os.path.dirname(os.path.abspath(str(PROCEDURAL[0]))), 'usd')
        usd_input_resource_folders = [os.path.join(USD_LIB, 'usd'), os.path.join(procedural_build, 'usd')]
        for usd_input_resource_folder in usd_input_resource_folders:
            if os.path.exists(usd_input_resource_folder):
                for entry in os.listdir(usd_input_resource_folder):
                    source_dir = os.path.join(usd_input_resource_folder, entry)
                    target_dir = os.path.join(usd_target_resource_folder, entry)
                    if os.path.isdir(source_dir) and not os.path.exists(target_dir):
                        shutil.copytree(source_dir, target_dir)
            # Also copy the plugInfo.
            shutil.copy2(os.path.join(USD_LIB, 'usd', 'plugInfo.json'), usd_target_resource_folder)

        if env['INSTALL_USD_PLUGIN_RESOURCES']:
            usd_plugin_resource_folder = os.path.join(USD_PATH, 'plugin', 'usd')
            if os.path.exists(usd_plugin_resource_folder):
                for entry in os.listdir(usd_plugin_resource_folder):
                    source_dir = os.path.join(usd_plugin_resource_folder, entry)
                    target_dir = os.path.join(usd_target_resource_folder, entry)
                    if os.path.isdir(source_dir) and not os.path.exists(target_dir):
                        shutil.copytree(source_dir, target_dir)


else:
    PROCEDURAL = None

if BUILD_DOCS:
    env.Tool('doxygen')
    docs_output = os.path.join(BUILD_BASE_DIR, 'docs')
    env['DOXYGEN_TAGS'] = {
        'OUTPUT_DIRECTORY': docs_output
    }
    DOCS = env.Doxygen(source='docs/Doxyfile', target=docs_output)
else:
    DOCS = None

# Generating plugInfo.json files so we have the right platform specific
# extension.

plugInfos = [
    (renderdelegateplugin_plug_info, renderdelegateplugin_out_plug_info),
    (scenedelegate_plug_info, scenedelegate_out_plug_info),
]

for (source, target) in plugInfos:
    env.Command(target=target, source=source,
                action=configure.configure_plug_info)

if BUILD_NDR_PLUGIN:
    env.Command(target=ndrplugin_out_plug_info,
                source=ndrplugin_plug_info,
                action=configure.configure_ndr_plug_info)
    
if BUILD_USD_IMAGING_PLUGIN:
    env.Command(target=usdimagingplugin_out_plug_info,
                source=usdimagingplugin_plug_info,
                action=configure.configure_usd_imaging_plug_info)

if RENDERDELEGATEPLUGIN:
    Depends(RENDERDELEGATEPLUGIN, renderdelegateplugin_plug_info)

if SCENEDELEGATE:
    Depends(SCENEDELEGATE, scenedelegate_plug_info)

# We now include the ndr plugin in the procedural, so we must add the plugInfo.json as well
if BUILD_PROCEDURAL and env['ENABLE_HYDRA_IN_USD_PROCEDURAL']:
    if BUILD_NDR_PLUGIN:
        procedural_ndr_plug_info = os.path.join(BUILD_BASE_DIR, 'plugins', 'procedural', 'usd', 'ndrArnold', 'resources', 'plugInfo.json')
        env.Command(target=procedural_ndr_plug_info,
                    source=ndrplugin_plug_info,
                        action=configure.configure_procedural_ndr_plug_info)
        Depends(PROCEDURAL, procedural_ndr_plug_info)

    if BUILD_USD_IMAGING_PLUGIN:
        procedural_imaging_plug_info = os.path.join(BUILD_BASE_DIR, 'plugins', 'procedural', 'usd', 'usdImagingArnold', 'resources', 'plugInfo.json')
        env.Command(target=procedural_imaging_plug_info,
                    source=usdimagingplugin_plug_info,
                    action=configure.configure_usd_imaging_proc_plug_info)
        Depends(PROCEDURAL, usdimagingplugin_plug_info)

    if BUILD_SCHEMAS:
        schemas_plug_info = os.path.join(schemas_build, 'source', 'plugInfo.json')
        schemas_file = os.path.join(schemas_build, 'source', 'generatedSchema.usda')
        schemas_out_plug_info = os.path.join(BUILD_BASE_DIR, 'plugins', 'procedural', 'usd', 'usdArnold', 'resources', 'plugInfo.json')
        schemas_out_file = os.path.join(BUILD_BASE_DIR, 'plugins', 'procedural', 'usd', 'usdArnold', 'resources', 'generatedSchema.usda')
        env.Command(schemas_out_plug_info, schemas_plug_info, Copy("$TARGET", "$SOURCE"))
        env.Command(schemas_out_file, schemas_file, Copy("$TARGET", "$SOURCE"))
        Depends(PROCEDURAL, SCHEMAS[0])
        Depends(PROCEDURAL, SCHEMAS[1])
    
if BUILD_TESTSUITE:
    if BUILD_PROCEDURAL:
        env['USD_PROCEDURAL_PATH'] = os.path.abspath(str(PROCEDURAL[0]))
    else:
        # if we're not building the procedural here, then we're using 
        # the procedural from the arnold SDK
        env['USD_PROCEDURAL_PATH'] = os.path.abspath(os.path.join(env['ARNOLD_PATH'], 'plugins', 'usd_proc{}'.format(system.LIB_EXTENSION)))

    # Target for the test suite
    TESTSUITE = env.SConscript(os.path.join('testsuite', 'SConscript'),
        variant_dir = testsuite_build,
        exports     = ['env'],
        duplicate   = 0)
    SConscriptChdir(1)
    '''
    This is currently causing issues when running the testsuite (see #746).
    We're disabling it for now, so devs will need to first build the repo 
    and then run the testsuite. This will also allow to run the tests on 
    a prebuilt library
    Depends(TESTSUITE, PROCEDURAL)
    if env['ENABLE_UNIT_TESTS']:
        if RENDERDELEGATE:
            Depends(TESTSUITE, RENDERDELEGATE)
        if NDRPLUGIN:
            Depends(TESTSUITE, NDRPLUGIN)
    '''
else:
    TESTSUITE = None

for target in [RENDERDELEGATEPLUGIN, PROCEDURAL, SCHEMAS, RENDERDELEGATE, DOCS, TESTSUITE, NDRPLUGIN, USDIMAGINGPLUGIN]:
    if target:
        if isinstance(target, dict):
            for t in target:
                env.AlwaysBuild(t)
        else:
            env.AlwaysBuild(target)

if TESTSUITE:
    env.Alias('testsuite', TESTSUITE['TESTSUITE_REPORT'])
env.Alias('install', PREFIX)

# Install compiled dynamic library
if PROCEDURAL:
    INSTALL_PROC = env.Install(PREFIX_PROCEDURAL, PROCEDURAL)
    if env['USD_BUILD_MODE'] == 'static':
        INSTALL_PROC += env.Install(PREFIX_PROCEDURAL, usd_target_resource_folder)
    env.Alias('procedural-install', INSTALL_PROC)

if RENDERDELEGATEPLUGIN:
    if is_windows:
        INSTALL_RENDERDELEGATE = env.Install(PREFIX_RENDER_DELEGATE, RENDERDELEGATEPLUGIN)
    else:
        INSTALL_RENDERDELEGATE = env.InstallAs(os.path.join(PREFIX_RENDER_DELEGATE, 'hdArnold%s' % system.LIB_EXTENSION), RENDERDELEGATEPLUGIN)
    INSTALL_RENDERDELEGATE += env.Install(os.path.join(PREFIX_RENDER_DELEGATE, 'hdArnold', 'resources'), [renderdelegateplugin_out_plug_info])
    INSTALL_RENDERDELEGATE += env.Install(PREFIX_RENDER_DELEGATE, ['plugInfo.json'])
    INSTALL_RENDERDELEGATE += env.Install(os.path.join(PREFIX_HEADERS, 'arnold_usd', 'render_delegate'), env.Glob(os.path.join('render_delegate', '*.h')))
    env.Alias('delegate-install', INSTALL_RENDERDELEGATE)
   
if NDRPLUGIN:
    if is_windows:
        INSTALL_NDRPLUGIN = env.Install(PREFIX_NDR_PLUGIN, NDRPLUGIN)
    else:
        INSTALL_NDRPLUGIN = env.InstallAs(os.path.join(PREFIX_NDR_PLUGIN, 'ndrArnold%s' % system.LIB_EXTENSION), NDRPLUGIN)
    INSTALL_NDRPLUGIN += env.Install(os.path.join(PREFIX_NDR_PLUGIN, 'ndrArnold', 'resources'), [ndrplugin_out_plug_info])
    INSTALL_NDRPLUGIN += env.Install(PREFIX_NDR_PLUGIN, ['plugInfo.json'])
    INSTALL_NDRPLUGIN += env.Install(os.path.join(PREFIX_HEADERS, 'arnold_usd', 'ndr'), env.Glob(os.path.join('ndr', '*.h')))
    env.Alias('ndrplugin-install', INSTALL_NDRPLUGIN)

if USDIMAGINGPLUGIN:
    if is_windows:
        INSTALL_USDIMAGINGPLUGIN = env.Install(PREFIX_USD_IMAGING_PLUGIN, USDIMAGINGPLUGIN)
    else:
        INSTALL_USDIMAGINGPLUGIN = env.InstallAs(os.path.join(PREFIX_USD_IMAGING_PLUGIN, 'usdImagingArnold%s' % system.LIB_EXTENSION), USDIMAGINGPLUGIN)
    INSTALL_USDIMAGINGPLUGIN += env.Install(os.path.join(PREFIX_USD_IMAGING_PLUGIN, 'usdImagingArnold', 'resources'), [usdimagingplugin_out_plug_info])
    INSTALL_USDIMAGINGPLUGIN += env.Install(PREFIX_USD_IMAGING_PLUGIN, ['plugInfo.json'])
    INSTALL_USDIMAGINGPLUGIN += env.Install(os.path.join(PREFIX_HEADERS, 'arnold_usd', 'usd_imaging'), env.Glob(os.path.join('usd_imaging', '*.h')))
    env.Alias('usdimagingplugin-install', INSTALL_USDIMAGINGPLUGIN)

if SCENEDELEGATE:
    if is_windows:
        INSTALL_SCENEDELEGATE = env.Install(PREFIX_SCENE_DELEGATE, SCENEDELEGATE)
    else:
        INSTALL_SCENEDELEGATE = env.InstallAs(os.path.join(PREFIX_SCENE_DELEGATE, 'imagingArnold%s' % system.LIB_EXTENSION), SCENEDELEGATE)
    INSTALL_SCENEDELEGATE += env.Install(os.path.join(PREFIX_SCENE_DELEGATE, 'imagingArnold', 'resources'), [scenedelegate_out_plug_info])
    INSTALL_SCENEDELEGATE += env.Install(PREFIX_SCENE_DELEGATE, ['plugInfo.json'])
    INSTALL_SCENEDELEGATE += env.Install(os.path.join(PREFIX_HEADERS, 'arnold_usd', 'scene_delegate'), env.Glob(os.path.join('scene_delegate', '*.h')))
    env.Alias('scenedelegate-install', INSTALL_SCENEDELEGATE)

# This follows the standard layout of USD plugins / libraries.
if SCHEMAS:
    INSTALL_SCHEMAS = env.Install(os.path.join(PREFIX_SCHEMAS), ['plugInfo.json'])
    INSTALL_SCHEMAS += env.Install(os.path.join(PREFIX_SCHEMAS, 'usdArnold', 'resources' ), [SCHEMAS[0], SCHEMAS[1]])
    INSTALL_SCHEMAS += env.Install(os.path.join(PREFIX_SCHEMAS, 'usdArnold', 'resources', 'usdArnold'), [SCHEMAS[0], SCHEMAS[2]])
    env.Alias('schemas-install', INSTALL_SCHEMAS)

if DOCS:
    INSTALL_DOCS = env.Install(PREFIX_DOCS, DOCS)
    env.Alias('docs-install', INSTALL_DOCS)

# We don't need to install the license if the prefix is left to its default #553
if PREFIX != '.':
    INSTALL_LICENSE = env.Install(PREFIX, 'LICENSE.md')
    env.Alias('license-install', INSTALL_LICENSE)

Default(PREFIX)
