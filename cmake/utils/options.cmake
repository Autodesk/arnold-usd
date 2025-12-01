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

option(USD_MONOLITHIC_BUILD "Monolithic build was used for USD." OFF)
option(BUILD_WITH_USD_STATIC "USD is built as a static, monolithic library." OFF)
option(TBB_STATIC_BUILD "TBB is built as a static library." OFF)
option(TBB_NO_EXPLICIT_LINKAGE "Explicit linkage of TBB libraries is disabled on windows." OFF)
option(BUILD_USE_CUSTOM_BOOST "Using a custom boost layout." OFF)
option(BUILD_BOOST_ALL_NO_LIB "Disable linking of boost libraries from boost headers." OFF)
option(BUILD_DISABLE_CXX11_ABI "Disable the use of the new CXX11 ABI" OFF)
option(BUILD_HEADERS_AS_SOURCES "Add headers are source files to the target to help when generating IDE projects." OFF)
option(ENABLE_HYDRA_IN_USD_PROCEDURAL "Enable hydra in the  procedural (experimental)" ON)
option(BUILD_SCENE_INDEX_PLUGIN "Build and enable scene index aka hydra 2" OFF)
option(ENABLE_SHARED_ARRAYS "Enable using shared arrays" OFF)
option(HYDRA_NORMALIZE_DEPTH "If true, return a normalized depth by using the P AOV. Otherwise, simply return the Z AOV for the depth" OFF)

set(USD_OVERRIDE_PLUGINPATH_NAME "PXR_PLUGINPATH_NAME" CACHE STRING "Override the plugin path name for the USD libraries. Used when running the testsuite with a static procedural")

option(BUILD_SCHEMAS "Builds the USD Schemas" ON)
option(BUILD_RENDER_DELEGATE "Builds the Render Delegate" ON)
option(BUILD_NDR_PLUGIN "Builds the NDR Plugin" ON)
option(BUILD_PROCEDURAL "Builds the Procedural" ON)
option(BUILD_PROC_SCENE_FORMAT "Enables the Procedural Scene format" ON)
option(BUILD_USD_IMAGING_PLUGIN "Builds the USD Imaging plugins" ON)
option(BUILD_SCENE_DELEGATE "Builds the Scene Delegate" OFF)
option(BUILD_DOCS "Builds the Documentation" OFF)
option(BUILD_TESTSUITE "Builds the testsuite" OFF)
option(BUILD_UNIT_TESTS "Build the unit tests" OFF)
option(BUILD_USE_PYTHON3 "Use python 3." ON)
option(BUILD_USDGENSCHEMA_ARNOLD "Build and use a custom usdgenschema" OFF)

set(PREFIX_PROCEDURAL "procedural" CACHE STRING "Directory to install the procedural under.")
set(PREFIX_PLUGINS "plugin" CACHE STRING "Directory to install the plugins (Hydra and Ndr) under.")
set(PREFIX_HEADERS "include" CACHE STRING "Directory to install the headers under.")
set(PREFIX_SCHEMA "schema" CACHE STRING "Directory to install the schemas under.")
set(PREFIX_BIN "bin" CACHE STRING "Directory to install the binaries under.")
set(PREFIX_DOCS "docs" CACHE STRING "Directory to install the documentation under.")

set(TEST_DIFF_HARDFAIL "0.0157" CACHE STRING "Hard failure of an image comparison test.")
set(TEST_DIFF_FAIL "0.00001" CACHE STRING "Failure of an image comparison test.")
set(TEST_DIFF_FAILPERCENT "33.334" CACHE STRING "Failure percentage of an image comparison test.")
set(TEST_DIFF_WARNPERCENT "0.0" CACHE STRING "Warning percentage of an image comparison test.")
set(TEST_RESOLUTION "160 120" CACHE STRING "Resolution of unit tests.")
set(TEST_MAKE_THUMBNAILS "Enables the generation of test thumbnails." ON)
option(TEST_WITH_HYDRA "Run the tests using the hydra procedural." OFF)

set(USD_PROCEDURAL_NAME "usd" CACHE STRING "Name of the usd procedural.")
set(USD_TRANSITIVE_STATIC_LIBS "" CACHE STRING "Usd transitive static libraries to embed in the procedural when usd is built in static monolithic. List of elements separated by a semi colon")
set(USD_TRANSITIVE_SHARED_LIBS "" CACHE STRING "Usd transitive libraries to link with. List of elements separated by a semi colon")
set(USD_TRANSITIVE_INCLUDE_DIRS "" CACHE STRING "Usd transitive include directory to compile with. List of elements separated by a semi colon")
