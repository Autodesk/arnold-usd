<!-- SPDX-License-Identifier: Apache-2.0 -->
Build Instructions
==================

Arnold USD is currently supported and tested on Windows, Linux and Mac, it can be built with [scons](#building-with-scons) or [cmake](#building-with-cmake).

## Dependencies

Arnold USD depends only on USD and Arnold so it is advised to use the same dependencies used to build USD, or the current VFX platform. We target USD versions starting from v20.08 up to the latest commit on the dev branch.

Python and Boost are optional if USD was build without Python support.

| Name | Version | Optional |
| --- | --- | --- |
| Arnold | 7.0.0+ | |
| USD | v20.08 - v25.11 | |
| Python | 2.7 |  x  |
| Boost | whatever version was used for USD |  x  |
| TBB | whatever version was used for USD | |

### Windows 10 & Python

Newer releases of Windows 10 ship with a set of app shortcuts that open the Microsoft Store to install Python 3 instead of executing the Python interpreter available in the path. This feature breaks the `abuild` script. To disable this, open the _Settings_ app, and search for _Manage app execution aliases_. On this page turn off any shortcut related to Python.

## Building with scons

Builds can be configured by either creating a `custom.py` file in the root of the cloned repository, or by passing the build flags to the `abuild` script. Once the configuration is set, use the `abuild` script to build the project and `abuild install` to install the project. Using `-j` when executing `abuild` instructs SCons to use multiple cores. For example: `abuild -j 8 install` will use 8 cores to build the project.

Sample `custom.py` files are provided in the _Examples_ section below.

### Build Options
- `MODE`: Sets the compilation mode, `opt` for optimized builds, `debug` for debug builds, and `profile` for optimized builds with debug information for profiling.
- `WARN_LEVEL: Warning level, `strict` enables errors for warnings, `warn-only` prints warnings and `none` turns of errors.
- `COMPILER`: Compiler to use. `gcc` or `clang` (default is `gcc`) on Linux and Mac, and `msvc` on Windows.
- `BUILD_SCHEMAS`: Whether or not to build the schemas and their wrapper.
- `BUILD_RENDER_DELEGATE`: Whether or not to build the hydra render delegate.
- `BUILD_NDR_PLUGIN`: Whether or not to build the node registry plugin.
- `BUILD_PROCEDURAL`: Whether or not to build the arnold procedural.
- `BUILD_TESTSUITE`: Whether or not to build the testsuite.
- `BUILD_DOCS`: Whether or not to build the documentation.
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
- `PREFIX_NDR_PLUGIN`: Directory to install the node registry plugin under. Defaults to `prefix/plugin`.
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

### Building for USD 20.05 and later on Linux

USD 20.05 requires GCC 6.3.1 and the use of C++-14. This can be achieved using the following snippet on CentOS 7. For other OSes replace the SHCXX line with a path to your local g++ 6.3.1 and later installation.

~~~python
SHCXX='/opt/rh/devtoolset-6/root/usr/bin/g++'
CXX_STANDARD='14'
DISABLE_CXX11_ABI=True
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
BUILD_DOCS=False

PREFIX=r'C:\solidAngle\arnold-usd'
~~~

#### Linux

Example `custom.py` for the default installation of Houdini 18.0.287 on Linux using the system's Python. Note, Houdini 18 requires the use of GCC 6.3.1 and C++ 14. The example is for a CentOS 7 installation, replace the value of SHCXX line with a path to your local install of GCC 6.3.1 and later if using a different distro.

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

SHCXX='/opt/rh/devtoolset-6/root/usr/bin/g++'
CXX_STANDARD='14'
DISABLE_CXX11_ABI=True

BUILD_SCHEMAS=False
BUILD_RENDER_DELEGATE=True
BUILD_NDR_PLUGIN=True
BUILD_PROCEDURAL=True
BUILD_TESTSUITE=True
BUILD_DOCS=True
~~~

## Building with CMake

We also support building the project with cmake to allow for greater flexibility and generating IDE projects for all major platforms. We require CMake 3.24. For controlling the build type, C++ standard, installation or embedding RPATHS, use the appropriate [CMake Variable](https://cmake.org/cmake/help/v3.24/manual/cmake-variables.7.html).

### Build Options

- `BUILD_SCHEMAS`: Whether or not to build the schemas and their wrapper.
- `BUILD_RENDER_DELEGATE`: Whether or not to build the hydra render delegate.
- `BUILD_NDR_PLUGIN`: Whether or not to build the node registry plugin.
- `BUILD_SCENE_INDEX`: Whether or not to build the scene index plugin.
- `BUILD_USD_IMAGING_PLUGIN`: Whether or not to build the usd imaging plugin.
- `BUILD_PROCEDURAL`: Whether or not to build the arnold procedural.
- `BUILD_BUNDLE`: Whether or not to build the ArnoldUsdBundle plugin containing all the usd plugins and arnold procedurals.
- `BUILD_TESTSUITE`: Whether or not to build the testsuite.
- `BUILD_UNIT_TESTS`: Whether or not to build the unit tests.
- `BUILD_DOCS`: Whether or not to build the documentation.
- `BUILD_DISABLE_CXX11_ABI`: Disabling the new C++ ABI introduced in GCC 5.1.
- `BUILD_HEADERS_AS_SOURCES`: Add headers are source files to the target to help when generating IDE projects.
- `BUILD_WITH_USD_STATIC`: Use the static USD libraries for the current build. This is mainly to compile the arnold usd procedural plugin and will deactivate all the usd plugins. 

### Dependencies Configuration:

- `ARNOLD_LOCATION`: Path to the Arnold SDK.
- `USD_LOCATION`: Path of the USD installation Root. Required if MAYAUSD_LOCATION or HOUDINI_LOCATION are not set.
- `MAYAUSD_LOCATION`: Path to the MayaUSD installation Root. optional, only if you compile with mayausd
- `MAYA_LOCATION`: Path to the Maya installation Root. optional, only if you compile with mayausd
- `HOUDINI_LOCATION`: Path to the Houdini installation Root. optional, only if you compile with houdini.
- `USD_INCLUDE_DIR`: Path to the USD Headers, optional. Use if not using a standard USD installation layout.
- `USD_LIBRARY_DIR`: Path to the USD Libraries, optional. Use if not using a standard USD installation layout.
- `USD_BINARY_DIR`: Path to the USD Executables, optional. Use if not using a standard USD installation layout.
- `USD_LIB_EXTENSION`: Extension of USD libraries, optional.
- `USD_STATIC_LIB_EXTENSION`: Extension of the static USD libraries, optional.
- `USD_LIB_PREFIX`: Prefix of USD libraries, optional.
- `USD_OVERRIDE_PLUGINPATH_NAME`: The PXR_PLUGINPATH_NAME environment variable name of the used USD library.
- `USD_TRANSITIVE_STATIC_LIBS`: If usd needs additional static libs like tbb, boost or python, they should be added in this variable, optional.
- `USD_TRANSITIVE_SHARED_LIBS`: If usd needs additional shared libs like tbb, boost or python, they should be added in this variable, optional.
- `GOOGLETEST_LOCATION`: Path to the Google Test Installation Root.
- `GOOGLETEST_LIB_EXTENSION`: Extension of Google Test libraries.
- `GOOGLETEST_LIB_PREFIX`: Prefix of Google Test libraries.
- `GTEST_INCLUDE_DIR`: Path to the Google Test Headers, optional. Use if not using a standard Google Test installation layout.
- `GTEST_LIBRARYDIR`: Path to the Google Test Libraries, optional. Use if not using a standard Google Test installation layout.

The build script will now try to find the `pxrConfig.cmake` file coming with any USD distribution for configuring USD dependencies. If for some reasons you do not want to use `pxrConfig.cmake`, make sure it is not in `USD_LOCATION` by moving it or renaming it, in that case the build script will try to find the usd libraries and you will have to set the locations of boost and tbb.

### Installation Configuration:

- `CMAKE_INSTALL_PREFIX`: Directory to install under.
- `PREFIX_PROCEDURAL`: Directory to install the procedural under.
- `PREFIX_PLUGINS`: Directory to install the plugins (hydra and node registry) under.
- `PREFIX_HEADERS`: Directory to install the headers under.
- `PREFIX_LIB`: Directory to install the libraries under.
- `PREFIX_BIN`: Directory to install the binaries under.
- `PREFIX_DOCS`: Directory to install the documentation under.

## Examples

In our examples we create a subfolder inside the source repository named `build` then run the cmake cmake configuration from there.

Note, we are setting `CMAKE_BUILD_TYPE` to `Release` in all the examples.

### Building on Linux

This example configures the project on Linux against a USD built using the official build scripts. In our example USD is installed at `/opt/USD`, google test is at `/opt/googletest`/, arnold is at `/opt/arnold`. We are using GCC 6.3.1+, see your distribution specific instructions for using the approprate GCC. Once configuration is done, you can use `make` and `make install` to build and install the project.

~~~bash
cmake ..
 -DCMAKE_BUILD_TYPE=Release
 -DARNOLD_LOCATION=/opt/arnold
 -DUSD_LOCATION=/opt/USD
 -DBUILD_UNIT_TESTS=ON
 -DGOOGLETEST_LOCATION=/opt/googletest
 -DCMAKE_CXX_STANDARD=14
 -DCMAKE_INSTALL_PREFIX=/opt/arnold-usd
~~~

This example builds the project against a standard, symlinked, installation of Houdini 18 on Centos. In our example Arnold is installed at `/opt/arnold`. We are using GCC 6.3.1+, see your distribution specific instructions for using the approprate GCC.

~~~bash
cmake ..
 -DCMAKE_BUILD_TYPE=Release
 -DARNOLD_LOCATION=/opt/arnold
 -DHOUDINI_LOCATION=/opt/hfs18.0
 -DBUILD_SCHEMAS=OFF
 -DBUILD_DISABLE_CXX11_ABI=ON
 -DCMAKE_CXX_STANDARD=14
 -DCMAKE_INSTALL_PREFIX=/opt/arnold-usd
~~~

### Building on Windows

On windows, it's also an option to use [cmake-gui](https://cmake.org/runningcmake/), instead of the command line.

This example configures arnold-usd using a stock build of USD 20.08 (using the supplied build script), installed at `C:\USD`, with arnold installed at `C:\arnold`, google test installed at `C:\googletest` and a default Python 2.7 installation. USD was built using Visual Studio 2019.

~~~bash
cmake ..
 -G "Visual Studio 16 2019"
 -DCMAKE_INSTALL_PREFIX=C:\arnold-usd
 -DCMAKE_CXX_STANDARD=14
 -DARNOLD_LOCATION=C:\arnold
 -DUSD_LOCATION=C:\USD
 -DBUILD_SCHEMAS=OFF
 -DBUILD_UNIT_TESTS=ON
 -DGOOGLETEST_LOCATION=C:\googletest
~~~


This example configures arnold-usd for Houdini 18.0.499 on Windows using the default installation folder, with arnold installed at `C:\arnold`. Note, as of 18.0.499, Houdini lacks inclusion of usdgenschema, so we have to disable generating the custom schemas.

~~~bash
cmake ..
 -G "Visual Studio 15 2017 Win64"
 -DCMAKE_INSTALL_PREFIX="C:\dist\arnold-usd"
 -DARNOLD_LOCATION="C:\arnold"
 -DHOUDINI_LOCATION="C:\Program Files\Side Effects Software\Houdini 18.0.499"
 -DBUILD_SCHEMAS=OFF
~~~
