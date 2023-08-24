
import platform


def update_plug_info(plug_info):
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
    f = open(plug_info, 'w')
    f.write(contents)