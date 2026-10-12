import os
import re
import sys

# Regression test for arnold-usd #2730.
#
# The render delegate needs to know where the OCIO config file resides in order to apply
# the correct color space. The OCIO environment variable takes precedence over everything
# else, and when it is not set the config path is taken from the "ocioConfigPath" render
# setting, which host applications such as maya set from their color management
# preferences.
#
# The "ocioConfigPath" render setting can only be pushed by a host application through the
# settings map, which kick has no way of doing, so this test covers the other half of the
# rule: the OCIO environment variable creates a color_manager_ocio configured with it, and
# nothing is configured when there is neither an environment variable nor a setting. This
# runs once per pass (usd / hydra / hydra2) with the pass-specific environment, so it
# guards the color manager setup of all the reading paths.

# A minimal but valid OCIO config, with a single color space named after the
# renderingColorSpace authored in test.usda.
OCIO_CONFIG = '''ocio_profile_version: 1

search_path:
strictparsing: false
luma: [0.2126, 0.7152, 0.0722]

roles:
  default: lin_test
  reference: lin_test
  scene_linear: lin_test
  data: lin_test
  color_timing: lin_test
  compositing_log: lin_test

displays:
  sRGB:
    - !<View> {name: raw, colorspace: lin_test}

active_displays: [sRGB]
active_views: [raw]

colorspaces:
  - !<ColorSpace>
    name: lin_test
    bitdepth: 32f
    isdata: false
    allocation: lg2
    allocationvars: [-15, 6]
'''

configPath = os.path.abspath('config.ocio')
with open(configPath, 'w') as f:
    f.write(OCIO_CONFIG)

kick = os.path.join(os.environ['ARNOLD_BINARIES'], 'kick')


def resave(scene, ocioEnvVar):
    '''Loads test.usda with kick and returns the contents of the resaved ass file'''
    if ocioEnvVar is None:
        os.environ.pop('OCIO', None)
    else:
        os.environ['OCIO'] = ocioEnvVar

    cmd = '{} test.usda -resave {} -r 16 16 -dw -dp -v 2 2>&1'.format(kick, scene)
    print(cmd)
    print(os.popen(cmd).read())

    if not os.path.exists(scene):
        return None
    with open(scene, 'r') as f:
        return f.read()


def getColorManagerConfig(content):
    '''Returns the config of the color_manager_ocio in the resaved scene, or None'''
    match = re.search(r'^color_manager_ocio\n\{(.*?)^\}', content, re.DOTALL | re.MULTILINE)
    if match is None:
        return None
    config = re.search(r'^\s*config\s+"([^"]*)"', match.group(1), re.MULTILINE)
    return config.group(1) if config else ''


errors = []

# 1) With the OCIO environment variable set, the color manager must use it
content = resave('with_env.ass', configPath)
if content is None:
    errors.append('kick did not resave the scene with the OCIO environment variable set')
else:
    config = getColorManagerConfig(content)
    if config is None:
        errors.append('no color_manager_ocio was created with the OCIO environment variable set')
    elif config != configPath:
        errors.append('color_manager_ocio config is "{}", expected "{}"'.format(config, configPath))
    if not re.search(r'^\s*color_manager\s+"', content, re.MULTILINE):
        errors.append('the options node does not reference the color manager')

# 2) Without an OCIO environment variable and without an "ocioConfigPath" render setting,
#    we fall back to arnold's default color manager and nothing points at our config
content = resave('without_env.ass', None)
if content is None:
    errors.append('kick did not resave the scene without the OCIO environment variable')
elif getColorManagerConfig(content) == configPath:
    errors.append('the OCIO config is used even though the environment variable is not set')

if errors:
    for e in errors:
        print('Failure! {}'.format(e))
    sys.exit(-1)

print('Success! The OCIO config path is resolved from the environment variable.')
