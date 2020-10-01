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
import os
import sys
import platform

def update_plug_info(plug_info):
    f = open(plug_info, 'r')
    contents = f.read()
    # Later USD versions correctly generate the CMake replaceable strings:
    contents = contents.replace('@PLUG_INFO_ROOT@', '..')
    contents = contents.replace('@PLUG_INFO_RESOURCE_PATH@', 'resources')
    if platform.system().lower() == 'linux':
        contents = contents.replace('"LibraryPath": "../../libusd.so"', '"LibraryPath": "../../libusdArnold.so"')
        contents = contents.replace('@PLUG_INFO_LIBRARY_PATH@', '../../libusdArnold.so')
    elif platform.system().lower() == 'darwin':
        contents = contents.replace('"LibraryPath": "../../libusd.dylib"', '"LibraryPath": "../../libusdArnold.dylib"')
        contents = contents.replace('@PLUG_INFO_LIBRARY_PATH@', '../../libusdArnold.dylib')
    else:
        contents = contents.replace('"LibraryPath": "../../usd.dll"', '"LibraryPath": "../../usdArnold.dll"')
        contents = contents.replace('@PLUG_INFO_LIBRARY_PATH@', '../../usdArnold.dll')
    f = open(plug_info, 'w')
    f.write(contents)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print 'Not enough arguments!'
        sys.exit(1)

    if not os.path.exists(sys.argv[1]):
        sys.exit(1)

    plug_info = sys.argv[1]

    update_plug_info(plug_info)
