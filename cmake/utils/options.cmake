# Copyright 2020 Autodesk, Inc.
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
option(USD_STATIC_BUILD "USD is built as a static, monolithic library." OFF)
option(TBB_STATIC_BUILD "TBB is built as a static library." OFF)
option(BUILD_USE_CUSTOM_BOOST "Using a custom boost layout." OFF)
option(BUILD_DISABLE_CXX11_ABI "Disable the use of the new CXX11 ABI" OFF)
option(BUILD_HEADERS_AS_SOURCES "Add headers are source files to the target to help when generating IDE projects." OFF)

option(BUILD_SCHEMAS "Builds the USD Schemas" ON)
option(BUILD_RENDER_DELEGATE "Builds the Render Delegate" ON)
option(BUILD_NDR_PLUGIN "Builds the NDR Plugin" ON)
option(BUILD_PROCEDURAL "Builds the Procedural" ON)
option(BUILD_USD_WRITER "Builds the USD Writer" ON)
option(BUILD_DOCS "Builds the Documentation" ON)

set(PREFIX_PROCEDURAL "procedural" CACHE STRING "Directory to install the procedural under.")
set(PREFIX_PLUGINS "plugin" CACHE STRING "Directory to install the plugins (Hydra and Ndr) under.")
set(PREFIX_HEADERS "include" CACHE STRING "Directory to install the headers under.")
set(PREFIX_LIB "lib" CACHE STRING "Directory to install the libraries under.")
set(PREFIX_BIN "bin" CACHE STRING "Directory to install the binaries under.")
set(PREFIX_DOCS "docs" CACHE STRING "Directory to install the documentation under.")