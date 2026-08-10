import os
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

# A polymesh with step_size > 0 is rendered as an implicit volume. Its
# standard_volume shader must be exported through the material's *volume* output,
# not the surface output. Otherwise, on import the shape is treated as a volume,
# the surface-bound shader is not picked up, and it falls back to _fallbackVolume
# (a black / non-emissive volume).
#
# #2615 (ARNOLD-17642) added volume-context export but only for native volume
# nodes; this covers the mesh-implicit-volume case. See MAXTOA-2033.

usdScene = 'test_resaved.usda'

AiBegin()
universe = AiUniverse()

camera = AiNode(universe, 'persp_camera', '/persp')
options = AiUniverseGetOptions(universe)
AiNodeSetStr(options, 'camera', '/persp')

vol_shader = AiNode(universe, 'standard_volume', '/mtl/volume_mtl/standard_volume0')
AiNodeSetFlt(vol_shader, 'density', 1.0)
AiNodeSetStr(vol_shader, 'emission_mode', 'density')
AiNodeSetFlt(vol_shader, 'emission', 1.0)
AiNodeSetRGB(vol_shader, 'emission_color', 1.0, 0.5, 0.1)

# A box rendered as an implicit volume (step_size > 0)
box = AiNode(universe, 'polymesh', '/volume_box')
AiNodeSetArray(box, 'vlist', AiArray(8, 1, AI_TYPE_VECTOR,
    AtVector(-1, -1, -1), AtVector(1, -1, -1),
    AtVector(-1,  1, -1), AtVector(1,  1, -1),
    AtVector(-1, -1,  1), AtVector(1, -1,  1),
    AtVector(-1,  1,  1), AtVector(1,  1,  1)))
AiNodeSetArray(box, 'nsides', AiArray(6, 1, AI_TYPE_UINT, 4, 4, 4, 4, 4, 4))
AiNodeSetArray(box, 'vidxs', AiArray(24, 1, AI_TYPE_UINT,
    0, 1, 3, 2,  4, 6, 7, 5,  0, 4, 5, 1,
    2, 3, 7, 6,  0, 2, 6, 4,  1, 5, 7, 3))
AiNodeSetFlt(box, 'step_size', 0.25)
AiNodeSetPtr(box, 'shader', vol_shader)

params = AiParamValueMap()
AiParamValueMapSetBool(params, 'binary', False)
success = AiSceneWrite(universe, usdScene, params)
AiParamValueMapDestroy(params)
AiUniverseDestroy(universe)
AiEnd()

if not success:
    print('ERROR: Scene export failed')
    sys.exit(-1)

# ---------------------------------------------------------------------------
#  Validate the resulting USD file
# ---------------------------------------------------------------------------
with open(usdScene, 'r') as f:
    lines = f.readlines()

errors = []

# sanity: the mesh must have been exported as a step-size volume
if not any('primvars:arnold:step_size' in line for line in lines):
    errors.append('step_size was not exported on the mesh')

# the volume shader must be connected to the material's volume output ...
if not any('outputs:arnold:volume.connect' in line and 'standard_volume0' in line
           for line in lines):
    errors.append('volume shader not connected to the material volume output')

# ... and must NOT be wired into the surface output
if any('outputs:arnold:surface.connect' in line and 'standard_volume0' in line
       for line in lines):
    errors.append('volume shader wired into the surface output')

if errors:
    for e in errors:
        print('FAIL: %s' % e)
    print('\n--- Generated USD file ---')
    print(''.join(lines))
    sys.exit(-1)

print('SUCCESS')
