
import json
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
