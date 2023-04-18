import os
import re
import shutil
import subprocess
## load our own python modules
import arnold.build as tools

def __method(env, *args):
    info = {}
    if 'VSWHERE_PATH' not in env:
        return info
    vswhere_version = tuple(int(x) for x in env['VSWHERE_VERSION'].split('.'))
    # Execute vswhere with args and collect the output for later parsing
    process_args = {
        'args'    : [env['VSWHERE_PATH']],
        'stdout'  : subprocess.PIPE,
        'stderr'  : subprocess.STDOUT,
        'universal_newlines': True,
    }
    if vswhere_version[0] >= 3:
        process_args['args'].append('-nocolor')
    process_args['args'].extend(list(args))
    process = subprocess.Popen(**process_args)
    output = []
    with process.stdout:
        for line in iter(process.stdout.readline, b''):
            if not line:
                break
            output.append(line.rstrip('\n'))
    process.wait()
    # Parse the vswhere output, looking for all the detected instances
    # and turn the dumped info into a list of dictionaries
    vs_instances = []
    if not process.returncode:
        regex = re.compile(r'^(?P<key>[^ \t]+): (?P<value>.+)$')
        for line in output:
            match = regex.match(line)
            if not match:
                continue
            key, value = match.group('key'), match.group('value')
            if key == 'instanceId':
                vs_instances.append({})
            vs_instances[-1][key] = value
    # Detection of the vcvars*.bat scripts (native x64)
    vcvars_args = {'arch':'64'}
    vcvars_list = [
        ['VC', 'Auxiliary', 'Build'    , 'vcvars{arch}.bat'], # VS2022 (17), VS2019 (16), VS2017 (15)
        ['VC', 'bin'      , 'amd{arch}', 'vcvars{arch}.bat'], # VS2015 (14), VS2013 (12), VS2012 (11)
    ]
    vc_instances = []
    for vs_instance in vs_instances:
        # Detect vcvars*.bat
        vs_version  = vs_instance.get('catalog_productDisplayVersion', vs_instance.get('installationVersion'))
        vs_version  = tuple(int(x) for x in vs_version.split('.'))
        vs_path     = vs_instance.get('installationPath')
        vcvars_path = None
        for vcvars in vcvars_list:
            path = os.path.join(vs_path, *vcvars).format(**vcvars_args)
            if os.path.isfile(path):
                vcvars_path = path
                break
        if not vcvars_path:
            continue
        # Detect VC
        vc_default_version = None
        vc_default_path    = None
        if vs_version[0] >= 15:
            path = os.path.join(vs_path, 'VC', 'Auxiliary', 'Build', 'Microsoft.VCToolsVersion.default.txt')
            if os.path.isfile(path):
                with open(path, 'r') as f:
                    vc_default_version = f.readlines()[0].strip()
            path = os.path.join(vs_path, 'VC', 'Tools', 'MSVC')
            if os.path.isdir(path):
                root, dirs, _ = os.walk(path).next()
                for vc_version in dirs:
                    path = os.path.join(root, vc_version, 'bin', 'Hostx{arch}', 'x{arch}', 'cl.exe').format(**vcvars_args)
                    if vc_version == vc_default_version and os.path.isfile(path):
                        vc_default_path = path
        else:
            vc_default_version = '.'.join(str(x) for x in vs_version)
            path = os.path.join(os.path.dirname(vcvars_path), 'cl.exe')
            if os.path.isfile(path):
                vc_default_path = path
        if not vc_default_version:
            continue
        # Detect MSBuild
        msbuild_path = None
        if vs_version[0] >= 15:
            path = os.path.join(vs_path, 'MSBuild', 'Current', 'Bin', 'amd{arch}', 'MSBuild.exe').format(**vcvars_args)
            if os.path.isfile(path):
                msbuild_path = path
        else:
            root = vs_path
            while not re.match(r'^[a-zA-Z]:\\$', root):
                path = os.path.join(root, 'MSBuild', '{}.0'.format(vs_version[0]), 'Bin', 'amd{arch}', 'MSBuild.exe').format(**vcvars_args)
                if os.path.isfile(path):
                    msbuild_path = path
                    break
                root = os.path.dirname(root)
            if not msbuild_path:
                continue
        vc_instances.append([vc_default_version, vc_default_path, vcvars_path, msbuild_path])
    return vc_instances

def generate(env, version='3.1.1'):
    # Download and cache vswhere.exe
    vswhere_path = os.path.join(os.path.dirname(__file__), '.cache', 'vswhere', version, 'vswhere.exe')
    if not os.path.exists(vswhere_path):
        vswhere_url = 'https://github.com/microsoft/vswhere/releases/download/{}/vswhere.exe'.format(version)
        env['DOWNLOADER'].get(vswhere_url, destination=vswhere_path)
    error, output = tools.system.execute([vswhere_path, '-help'])
    if not error:
        regex = re.compile(r'^Visual Studio Locator version (?P<version>\d+\.\d+\.\d+)\+(?P<commit>[0-9a-f]{10}) ')
        match = regex.search(output[0].decode('ascii'))
        if match and match.groupdict().get('version') == version:
            env['VSWHERE_PATH']    = vswhere_path
            env['VSWHERE_VERSION'] = version
            env.AddMethod(__method, 'vswhere')
            return
    raise SCons.Errors.StopError('Unable to get vswhere {}'.format(version))

def exists(env):
    return 'VSWHERE_PATH' in env
