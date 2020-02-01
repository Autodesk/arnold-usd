Build Instructions
==================

Arnold USD is currently supported and tested on Windows, Linux and Mac.

## Dependencies

It is advised to use the same dependencies used to build USD, or the current VFX platform. We target USD versions starting from v19.01 up to the latest commit on the dev branch.

Python and Boost are optional if USD was build without Python support.

| Name | Version | Optional |
| --- | --- | --- |
| Arnold | 5.4+ | |
| USD | v19.01 - dev | |
| Python | 2.7 |  x  |
| Boost | 1.55 (Linux), 1.61 (Mac, Windows VS 2015), 1.65.1 (Windows VS 2017) or 1.66.0 (VFX platform) |  x  |
| TBB | 4.4 Update 6 or 2018 (VFX platform) | |

### Windows 10 & Python

Newer releases of Windows 10 ship with a set of app shortcuts that open the Microsoft Store to install Python 3 instead of executing the Python interpreter available in the path. This feature breaks the `abuild` script. To disable this, open the _Settings_ app, and search for _Manage app execution aliases_. On this page turn off any shortcut related to Python.

## Building

Builds can be configured by either creating a `custom.py` file in the root of the cloned repository, or by passing the build flags to the `abuild` script. Once the configuration is set, use the `abuild` script to build the project and `abuild install` to install the project. Using `-j` when executing `abuild` instructs SCons to use multiple cores. For example: `abuild -j 8 install` will use 8 cores to build the project.

Sample `custom.py` files are provided in the _Examples_ section below.

### Build Options
- `MODE`: Sets the compilation mode, `opt` for optimized builds, `debug` for debug builds, and `profile` for optimized builds with debug information for profiling.
- `WARN_LEVEL: Warning level, `strict` enables errors for warnings, `warn-only` prints warnings and `none` turns of errors.
- `COMPILER`: Compiler to use. `gcc` or `clang` (default is `gcc`) on Linux and Mac, and `msvc` on Windows.
- `BUILD_SCHEMAS`: Whether or not to build the schemas and their wrapper.
- `BUILD_RENDER_DELEGATE`: Whether or not to build the hydra render delegate.
- `BUILD_NDR_PLUGIN`: Whether or not to build the node registry plugin.
- `BUILD_USD_WRITER`: Whether or not to build the arnold to usd writer tool.
- `BUILD_PROCEDURAL`: Whether or not to build the arnold procedural.
- `BUILD_TESTSUITE`: Whether or not to build the testsuite.
- `BUILD_DOCS`: Whether or not to build the documentation.
- `BUILD_FOR_KATANA`: Whether or not the build is using usd libs shipped in Katana.
- `DISABLE_CXX11_ABI`: Disabling the new C++ ABI introduced in GCC 5.1.

### Dependencies Configuration
- `ARNOLD_PATH`: Path to the Arnold SDK.
- `ARNOLD_API_INCLUDES`: Path to the Arnold API include files. Set to `$ARNOLD_PATH/include` by default.
- `ARNOLD_API_LIB`: Path to the Arnold API Library files. Set to `$ARNOLD_PATH/bin` on Linux and Mac, and `$ARNOLD_PATH/lib` on Windows by default.
- `ARNOLD_BINARIES`: Path to the Arnold API Executable files. Set to `$ARNOLD_PATH/bin` by default.
- `ARNOLD_PYTHON`: Path to the Arnold API Python files. Set to `$ARNOLD_PATH/bin` by default.
- `USD_PATH`: Path to the USD Installation Root.
- `USD_INCLUDE`: Path to the USD Headers. Set to `$USD_PATH/include` by default.
- `USD_LIB`: Path to the USD Libraries. Set to `$USD_PATH/lib` by default.
- `USD_BIN`: Path to the USD Executables. Set to `$USD_PATH/include` by default.
- `USD_BUILD_MODE`: Build mode of USD. `shared_libs` is when there is a separate library for each module, `monolithic` is when all the modules are part of a single library and `static` is when there is a singular static library for all USD. Note, `static` only supported when building the procedural and the usd -> ass converter.
- `USD_MONOLITHIC_LIBRARY`: Name of the USD monolithic library'. By default it is `usd_ms`.
- `USD_LIB_PREFIX`: USD library name prefix. By default it is set to `lib` on all platforms.
- `BOOST_INCLUDE`: Where to find the boost headers. By default this points inside the USD installation, and works when USD is deployed using the official build scripts.
- `BOOST_LIB`: Where to find the Boost Libraries.
- `BOOST_LIB_NAME`: Boost library name pattern. By default it is set to `boost_%s`, meaning scons will look for boost_python.
- `PYTHON_INCLUDE`: Where to find the Python Includes. This is only required if USD is built with Python support. See below.
- `PYTHON_LIB`: Where to find the Python Library. This is only required if USD is built with Python support. See below.
- `PYTHON_LIB_NAME`: Name of the python library. By default it is `python27`.
- `TBB_INCLUDE`: Where to find TBB headers.
- `TBB_LIB`: Where to find TBB libraries.

### Installation Configuration
- `PREFIX`: Directory to install under.
- `PREFIX_PROCEDURAL`: Directory to install the procedural under. Defaults to `prefix/procedural`.
- `PREFIX_RENDER_DELEGATE`: Directory to install the render delegate under. Defaults to `prefix/plugin`.
- `PREFIX_NDR_PLUGIN`: Directory to install the ndr plugin under. Defaults to `prefix/plugin`.
- `PREFIX_HEADERS`: Directory to install the headers under. Defaults to `prefix/include`.
- `PREFIX_LIB`: Directory to install the libraries under. Defaults to `prefix/lib`.
- `PREFIX_BIN`: Directory to install the binaries under. Defaults to `prefix/bin`.
- `PREFIX_DOCS`: Directory to install the documentation under. Defaults to `prefix/docs`.
- `PREFIX_THIRD_PARTY`: Directory to install the third party modules under. Defaults to `prefix/third_party`.

## Examples

### Building on Linux

This simple example `custom.py` builds the project on Linux, against the system Python libraries and a monolithic USD build.

~~~python
ARNOLD_PATH='/opt/autodesk/arnold-5.4.0.0'
USD_PATH='/opt/pixar/USD'
USD_BUILD_MODE='monolithic'
BOOST_INCLUDE='/usr/include'
PYTHON_INCLUDE='/usr/include/python2.7'
PYTHON_LIB='/usr/lib'
PYTHON_LIB_NAME='python2.7'
PREFIX='/opt/autodesk/arnold-usd'
~~~

This is the same configuration, this time passed directly to `abuild` on the command line:

~~~bash
abuild ARNOLD_PATH='/opt/autodesk/arnold-5.4.0.0' USD_PATH='/opt/pixar/USD' USD_BUILD_MODE='monolithic' BOOST_INCLUDE='/usr/include' PYTHON_INCLUDE='/usr/include/python2.7' PYTHON_LIB='/usr/lib' PYTHON_LIB_NAME='python2.7' PREFIX='/opt/autodesk/arnold-usd'
~~~

### Building for Katana

We support building against the libraries shipping with Katana 3.2+ and support using the Render Delegate in the Hydra viewport. The example `custom.py` below is for building the Render Delegate for Katana's Hydra Viewport, where Katana is installed in `/opt/Katana3.2v1`. The most important flag is `BUILD_FOR_KATANA` which changes the build on Linux to support the uniquely named (like: `Fnusd.so`) USD libraries shipped in Katana for Linux. When using a newer compiler to build the render delegate (e.g. GCC 6.3.1 from the VFX Platform), set `DISABLE_CXX11_ABI` to _True_ to disable the new C++ ABI introduced in GCC 5.1, as the [VFX Platform suggests](https://vfxplatform.com/#footnote-gcc6). 

~~~python
ARNOLD_PATH='/opt/autodesk/arnold-5.4.0.0'
USD_PATH='./'
USD_INCLUDE='/opt/Katana3.2v1/include'
USD_LIB='/opt/Katana3.2v1/bin'
BOOST_INCLUDE='/usr/include'
PYTHON_INCLUDE='/usr/include/python2.7'
PYTHON_LIB='/usr/lib'
PYTHON_LIB_NAME='python2.7'
USD_BUILD_MODE='shared_libs'
BUILD_SCHEMAS=False
BUILD_RENDER_DELEGATE=True
BUILD_PROCEDURAL=False
BUILD_TESTSUITE=False
BUILD_USD_WRITER=False
BUILD_DOCS=False
BUILD_FOR_KATANA=True
DISABLE_CXX11_ABI=True
PREFIX='/opt/autodesk/arnold-usd'
~~~

### Building for Houdini

We support building against the libraries shipping with Houdini 18.0+ and using the render delegate in the Solaris viewport. As of now (18.0.287) Houdini does not ship `usdGenSchema`, so the schemas target cannot be built against Houdini. Houdini prefixes standard USD library names with `libpxr_` (i.e. `libusd.so` becomes `libpxr_usd.so`), which can be configured via the `USD_LIB_PREFIX` variable. On Linux and macOS, Boost libraries are prefixed with `h`, on Windows Boost libraries are prefixed with `h` and suffixed with `-mt` (i.e. `boost_python.lib` becomes `hboost_python-mt.lib`), which requires setting the `BOOST_LIB_NAME` variable.

#### Mac

Here's an example `custom.py` for the default installation of Houdini 18.0.287 on macOS:

~~~python
ARNOLD_PATH='/opt/solidAngle/arnold'

WARN_LEVEL='warn-only'

USD_PATH='./'
USD_LIB='/Applications/Houdini/Houdini18.0.287/Frameworks/Houdini.framework/Versions/Current/Libraries'
USD_INCLUDE='/Applications/Houdini/Houdini18.0.287/Frameworks/Houdini.framework/Versions/Current/Resources/toolkit/include'
USD_BIN='/Applications/Houdini/Houdini18.0.287/Frameworks/Houdini.framework/Versions/Current/Resources/bin'
USD_BUILD_MODE='shared_libs'
USD_LIB_PREFIX='libpxr_'

BOOST_INCLUDE='/Applications/Houdini/Houdini18.0.287/Frameworks/Houdini.framework/Versions/Current/Resources/toolkit/include/hboost'
BOOST_LIB='/Applications/Houdini/Houdini18.0.287/Frameworks/Houdini.framework/Versions/Current/Libraries'
BOOST_LIB_NAME='hboost_%s'

PYTHON_INCLUDE='/Applications/Houdini/Houdini18.0.287/Frameworks/Python.framework/Versions/2.7/include/python2.7'
PYTHON_LIB='/Applications/Houdini/Houdini18.0.287/Frameworks/Python.framework/Versions/2.7/lib'
PYTHON_LIB_NAME='python2.7'

BUILD_SCHEMAS=False
BUILD_RENDER_DELEGATE=True
BUILD_USD_WRITER=True
BUILD_PROCEDURAL=True
BUILD_TESTSUITE=True
BUILD_DOCS=True

PREFIX='/opt/solidAngle/arnold-usd'
~~~

#### Windows

Example `custom.py` for the default installation of Houdini 18.0.287 on Windows:

~~~python
ARNOLD_PATH=r'C:\solidAngle\arnold'

USD_PATH='./'
USD_BIN='./'
USD_INCLUDE=r'C:\Program Files\Side Effects Software\Houdini 18.0.287\toolkit\include'
USD_LIB=r'C:\Program Files\Side Effects Software\Houdini 18.0.287\custom\houdini\dsolib'
USD_LIB_PREFIX='libpxr_'

BOOST_INCLUDE=r'C:\Program Files\Side Effects Software\Houdini 18.0.287\toolkit\include\hboost'
BOOST_LIB=r'C:\Program Files\Side Effects Software\Houdini 18.0.287\custom\houdini\dsolib'
BOOST_LIB_NAME='hboost_%s-mt'

PYTHON_INCLUDE=r'C:\Program Files\Side Effects Software\Houdini 18.0.287\toolkit\include\python2.7'
PYTHON_LIB=r'c:\Program Files\Side Effects Software\Houdini 18.0.287\python27\libs'
PYTHON_LIB_NAME='python27'

USD_BUILD_MODE='shared_libs'

BUILD_SCHEMAS=False
BUILD_RENDER_DELEGATE=True
BUILD_PROCEDURAL=True
BUILD_TESTSUITE=True
BUILD_USD_WRITER=True
BUILD_DOCS=False

PREFIX=r'C:\solidAngle\arnold-usd'
~~~

#### Linux

Example `custom.py` for the default installation of Houdini 18.0.287 on Linux using the system's Python:

~~~python
ARNOLD_PATH='/opt/solidAngle/arnold'

USD_PATH='./'
USD_BIN='./'
USD_INCLUDE='/opt/hfs18.0/toolkit/include'
USD_LIB='/opt/hfs18.0/dsolib'
USD_LIB_PREFIX='libpxr_'

PREFIX='/opt/solidAngle/arnold-usd'

BOOST_INCLUDE='/opt/hfs18.0/toolkit/include/hboost'
BOOST_LIB='/opt/hfs18.0/dsolib'
BOOST_LIB_NAME='hboost_%s'

PYTHON_INCLUDE='/usr/include/python2.7'
PYTHON_LIB='/usr/lib'
PYTHON_LIB_NAME='python2.7'

USD_BUILD_MODE='shared_libs'

DISABLE_CXX11_ABI=True
BUILD_SCHEMAS=False
BUILD_RENDER_DELEGATE=True
BUILD_PROCEDURAL=True
BUILD_TESTSUITE=True
BUILD_USD_WRITER=True
BUILD_DOCS=True
~~~
