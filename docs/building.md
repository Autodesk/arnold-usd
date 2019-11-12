Building Arnold to USD
======================

# Supported Platforms

The toolset is currently supported and tested on Windows, Linux and Mac.

# Dependencies

Prefer to use the same dependencies used to build USD, otherwise prefer
the versions listed in the current VFX platform. We target USD versions starting
from v19.01 up to the latest commit on the dev branch.

Python and Boost is optional if USD was build without Python support.

| Name | Version | Optional |
| --- | --- | --- |
| Arnold | 5.4.0.0 | |
| USD | v19.01 - dev | |
| Python | 2.7 | x |
| Boost | 1.55 (Linux), 1.61 (Mac, Windows VS 2015), 1.65.1 (Windows VS 2017) or 1.66.0 (VFX platform) | x |
| TBB | 4.4 Update 6 or 2018 (VFX platform) | |

# Configuring build

Builds can be configured by either creating a `custom.py` file in the root
of the cloned repository, or pass the build flags directly. The notable options
are the following.

## Configuring Build
- MODE: Sets the compilation mode, `opt` for optimized builds, `debug` for debug builds, and `profile` for optimized builds with debug information for profiling.
- WARN_LEVEL: Warning level, `strict` enables errors for warnings, `warn-only` prints warnings and `none` turns of errors.
- COMPILER: Compiler to use. `gcc` or `clang` (default is `gcc`) on Linux and Mac, and `msvc` on Windows.
- BUILD_SCHEMAS: Wether or not to build the schemas and their wrapper.
- BUILD_RENDER_DELEGATE: Wether or not to build the hydra render delegate.
- BUILD_USD_WRITER: Wether or not to build the arnold to usd writer tool.
- BUILD_PROCEDURAL: Wether or not to build the arnold procedural.
- BUILD_TESTSUITE: Wether or not to build the testsuite.
- BUILD_DOCS: Wether or not to build the documentation.
- BUILD_FOR_KATANA: Wether or not the build is using usd libs shipped in Katana.
- BUILD_HOUDINI_TOOLS: Wether or not to build the Houdini specific tools.
- DISABLE_CXX11_ABI: Disabling the new C++ ABI introduced in GCC 5.1.

## Configuring Dependencies
- ARNOLD_PATH: Path to the Arnold SDK.
- ARNOLD_API_INCLUDES: Path to the Arnold API include files. Set to `$ARNOLD_PATH/include` by default.
- ARNOLD_API_LIB: Path to the Arnold API Library files. Set to `$ARNOLD_PATH/bin` on Linux and Mac, and `$ARNOLD_PATH/lib` on Windows by default.
- ARNOLD_BINARIES: Path to the Arnold API Executable files. Set to `$ARNOLD_PATH/bin` by default.
- ARNOLD_PYTHON: Path to the Arnold API Python files. Set to `$ARNOLD_PATH/bin` by default.
- USD_PATH: Path to the USD Installation Root.
- USD_INCLUDE: Path to the USD Headers. Set to `$USD_PATH/include` by default.
- USD_LIB: Path to the USD Libraries. Set to `$USD_PATH/lib` by default.
- USD_BIN: Path to the USD Executables. Set to `$USD_PATH/include` by default.
- USD_BUILD_MODE: Build mode of USD. `shared_libs` is when there is a separate library for each module, `monolithic` is when all the modules are part of a single library and `static` is when there is a singular static library for all USD. Note, `static` only supported when building the procedural and the usd -> ass converter.
- USD_MONOLITHIC_LIBRARY: Name of the USD monolithic library'. By default it is `usd_ms`.
- BOOST_INCLUDE: Where to find the boost headers. By default this points inside the USD installation, and works when USD is deployed using the official build scripts.
- BOOST_LIB: Where to find the Boost Libraries.
- BOOST_LIB_NAME: Boost library name pattern. By default it is set to `boost_%s`, meaning scons will look for boost_python.
- PYTHON_INCLUDE: Where to find the Python Includes. This is only required if USD is built with Python support. See below.
- PYTHON_LIB: Where to find the Python Library. This is only required if USD is built with Python support. See below.
- PYTHON_LIB_NAME: Name of the python library. By default it is `python27`.
- USD_HAS_PYTHON_SUPPORT: Whether or not USD was built with Python support. If it was, Boost and Python dependencies are required.
- TBB_INCLUDE: Where to find TBB headers.
- TBB_LIB: Where to find TBB libraries.

## Configuring Installation
- PREFIX: Directory to install under. True by default.
- PREFIX_PROCEDURAL: Directory to install the procedural under. True by default.
- PREFIX_RENDER_DELEGATE: Directory to install the procedural under. True by default.
- PREFIX_HEADERS: Directory to install the headers under. True by default.
- PREFIX_LIB: Directory to install the libraries under. True by default.
- PREFIX_BIN: Directory to install the binaries under. True by default.
- PREFIX_DOCS: Directory to install the documentation under. True by default.

## Example configuration

This builds the project on Linux, against the Distro supplied Python libraries and a monolithic USD build.

```
ARNOLD_PATH='/opt/autodesk/arnold-5.4.0.0'
USD_PATH='/opt/pixar/USD'
USD_HAS_PYTHON_SUPPORT=True
USD_BUILD_MODE='monolithic'
BOOST_INCLUDE='/usr/include'
PYTHON_INCLUDE='/usr/include/python2.7'
PYTHON_LIB='/usr/lib'
PYTHON_LIB_NAME='python2.7'
PREFIX='/opt/autodesk/arnold-usd'
```

# Building for Katana 3.2+

We support building against the shipped libraries in Katana and support using the Render Delegate in the Hydra viewport. The example below is for building the Render DElegate for Katana's Hydra Viewport, where Katana is installed at `/opt/Katana3.2v1`. The most important flag is `BUILD_FOR_KATANA` which changes the build on Linux to support the uniquely named (like: `Fnusd.so`) usd libraries shipped in Katana for Linux. When using a newer compiler to build the render delegate (like GCC 6.3.1 from the vfx platform), set DISABLE_CXX11_ABI to True to disable the new C++ ABI introduced in GCC 5.1, as the vfx platform suggests. 

```
ARNOLD_PATH='/opt/autodesk/arnold-5.4.0.0'
USD_PATH='./'
USD_INCLUDE='/opt/Katana3.2v1/include'
USD_LIB='/opt/Katana3.2v1/bin'
USD_HAS_PYTHON_SUPPORT=True
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
```
