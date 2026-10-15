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
from __future__ import print_function
import json
import os
import sys
import platform

def set_library_path(contents, library_path):
    # Turn the schema plugin into a library plugin loading library_path (relative to the plugin root).
    # usdGenSchema and usdGenSchemaArnold don't write the same plugin type, so we edit the json
    # rather than the text. The header comments written by usdGenSchema are preserved.
    lines = contents.splitlines(True)
    header_size = 0
    while header_size < len(lines) and lines[header_size].lstrip().startswith('#'):
        header_size += 1
    data = json.loads(''.join(lines[header_size:]))
    for plugin in data.get('Plugins', []):
        plugin['Type'] = 'library'
        plugin['LibraryPath'] = library_path
    return ''.join(lines[:header_size]) + json.dumps(data, indent=4) + '\n'

def update_plug_info(plug_info, library_path = None):
    f = open(plug_info, 'r')
    contents = f.read()
    # Later USD versions correctly generate the CMake replaceable strings:
    contents = contents.replace('@PLUG_INFO_ROOT@', '..')
    contents = contents.replace('@PLUG_INFO_RESOURCE_PATH@', 'resources')
    if platform.system().lower() == 'linux':
        contents = contents.replace('"LibraryPath": "../../libusd.so"', '"LibraryPath": ""')
        contents = contents.replace('@PLUG_INFO_LIBRARY_PATH@', '')
    elif platform.system().lower() == 'darwin':
        contents = contents.replace('"LibraryPath": "../../libusd.dylib"', '"LibraryPath": ""')
        contents = contents.replace('@PLUG_INFO_LIBRARY_PATH@', '')
    else:
        contents = contents.replace('"LibraryPath": "../../usd.dll"', '"LibraryPath": ""')
        contents = contents.replace('@PLUG_INFO_LIBRARY_PATH@', '')
    # An empty library path is used when the code is embedded in the executable loading the schemas
    if library_path is not None:
        contents = set_library_path(contents, library_path)
    f = open(plug_info, 'w')
    f.write(contents)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Not enough arguments!')
        sys.exit(1)

    if not os.path.exists(sys.argv[1]):
        sys.exit(1)

    plug_info = sys.argv[1]
    # Optional path of the usdArnold library, relative to the plugin root.
    # --embedded is used when the library code is linked in the executable (e.g. turntable)
    library_path = None
    if len(sys.argv) > 2:
        library_path = '' if sys.argv[2] == '--embedded' else sys.argv[2]

    update_plug_info(plug_info, library_path)
