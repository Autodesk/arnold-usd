import os
import re
import sys

# Regression test for ARNOLD-18814.
#
# The color_space_linear of the color manager can come from the UsdRenderSettings builtin
# renderingColorSpace attribute or from the arnold:global:color_space_linear attribute, which
# takes precedence. This runs once per pass (usd / hydra / hydra2) with the pass-specific
# environment, so it checks the usd procedural, Hydra 1 and the Hydra 2 render settings prim.

# A minimal OCIO config with the two color spaces authored in the scenes. Its scene_linear role
# is a third color space, so that a missing color_space_linear can't be mistaken for one of them.
OCIO_CONFIG = '''ocio_profile_version: 1

search_path:
strictparsing: false
luma: [0.2126, 0.7152, 0.0722]

roles:
  default: lin_default
  reference: lin_default
  scene_linear: lin_default
  data: lin_default
  color_timing: lin_default
  compositing_log: lin_default

displays:
  sRGB:
    - !<View> {name: raw, colorspace: lin_default}

active_displays: [sRGB]
active_views: [raw]

colorspaces:
  - !<ColorSpace>
    name: lin_default
    bitdepth: 32f
    isdata: false
  - !<ColorSpace>
    name: lin_a
    bitdepth: 32f
    isdata: false
  - !<ColorSpace>
    name: lin_b
    bitdepth: 32f
    isdata: false
'''

configPath = os.path.abspath('config.ocio')
with open(configPath, 'w') as f:
    f.write(OCIO_CONFIG)
# The color manager is only created with a config, which kick can only get from the environment
os.environ['OCIO'] = configPath

kick = os.path.join(os.environ['ARNOLD_BINARIES'], 'kick')


def getColorSpaceLinear(scene):
    '''Loads the scene with kick and returns the color_space_linear of the resaved color manager'''
    resaved = os.path.splitext(scene)[0] + '.ass'
    cmd = '{} {} -resave {} -r 16 16 -dw -dp -v 2 2>&1'.format(kick, scene, resaved)
    print(cmd)
    print(os.popen(cmd).read())
    if not os.path.exists(resaved):
        return None
    with open(resaved, 'r') as f:
        content = f.read()
    match = re.search(r'^color_manager_ocio\n\{(.*?)^\}', content, re.DOTALL | re.MULTILINE)
    if match is None:
        return None
    colorSpace = re.search(r'^\s*color_space_linear\s+"([^"]*)"', match.group(1), re.MULTILINE)
    return colorSpace.group(1) if colorSpace else ''


errors = []
for scene, expected in [('with_global.usda', 'lin_a'), ('without_global.usda', 'lin_b')]:
    colorSpace = getColorSpaceLinear(scene)
    if colorSpace is None:
        errors.append('{}: no color_manager_ocio in the resaved scene'.format(scene))
    elif colorSpace != expected:
        errors.append('{}: color_space_linear is "{}", expected "{}"'.format(scene, colorSpace, expected))

if errors:
    for e in errors:
        print('Failure! {}'.format(e))
    sys.exit(-1)

print('Success! arnold:global:color_space_linear takes precedence over renderingColorSpace.')
