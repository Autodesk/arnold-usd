import os
import re
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

AiBegin()

usdScene = 'test_resaved.usda'
NUM_FRAMES = 5
APPEAR_FRAME = 2

for frame in range(NUM_FRAMES):
    universe = AiUniverse()

    options = AiUniverseGetOptions(universe)
    AiNodeSetFlt(options, 'fps', 24.0)

    camera = AiNode(universe, 'persp_camera', 'cam')
    AiNodeSetStr(options, 'camera', '/cam')

    # A box that exists on every frame
    box = AiNode(universe, 'polymesh', '/always_here')
    AiNodeSetArray(box, 'vlist', AiArray(8, 1, AI_TYPE_VECTOR,
        AtVector(-1, -1, -1), AtVector(1, -1, -1),
        AtVector(-1,  1, -1), AtVector(1,  1, -1),
        AtVector(-1, -1,  1), AtVector(1, -1,  1),
        AtVector(-1,  1,  1), AtVector(1,  1,  1)))
    AiNodeSetArray(box, 'nsides', AiArray(6, 1, AI_TYPE_UINT, 4, 4, 4, 4, 4, 4))
    AiNodeSetArray(box, 'vidxs', AiArray(24, 1, AI_TYPE_UINT,
        0, 1, 3, 2,  4, 6, 7, 5,  0, 4, 5, 1,
        2, 3, 7, 6,  0, 2, 6, 4,  1, 5, 7, 3))

    always_shader = AiNode(universe, 'standard_surface', '/always_shader')
    AiNodeSetRGB(always_shader, 'base_color', 0.2, 0.8, 0.2)
    AiNodeSetPtr(box, 'shader', always_shader)

    # The late shader exists in every frame (simulating a material that is
    # always present in the scene), but its geometry only appears on
    # frame >= APPEAR_FRAME.
    
    if frame >= APPEAR_FRAME:

        late_shader = AiNode(universe, 'standard_surface', '/late_shader')
        AiNodeSetRGB(late_shader, 'base_color', 0.8, 0.1, 0.1)
        AiNodeSetFlt(late_shader, 'specular_roughness', 0.3)

        late_obj = AiNode(universe, 'polymesh', '/late_object')
        AiNodeSetArray(late_obj, 'vlist', AiArray(8, 1, AI_TYPE_VECTOR,
            AtVector(-1, -1, 2), AtVector(1, -1, 2),
            AtVector(-1,  1, 2), AtVector(1,  1, 2),
            AtVector(-1, -1, 4), AtVector(1, -1, 4),
            AtVector(-1,  1, 4), AtVector(1,  1, 4)))
        AiNodeSetArray(late_obj, 'nsides', AiArray(6, 1, AI_TYPE_UINT, 4, 4, 4, 4, 4, 4))
        AiNodeSetArray(late_obj, 'vidxs', AiArray(24, 1, AI_TYPE_UINT,
            0, 1, 3, 2,  4, 6, 7, 5,  0, 4, 5, 1,
            2, 3, 7, 6,  0, 2, 6, 4,  1, 5, 7, 3))

        AiNodeSetMatrix(late_obj, 'matrix', AtMatrix(
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            3, 0, 0, 1))

        AiNodeSetPtr(late_obj, 'shader', late_shader)

    params = AiParamValueMap()
    AiParamValueMapSetBool(params, 'binary', False)
    AiParamValueMapSetFlt(params, 'frame', float(frame))
    AiParamValueMapSetBool(params, 'append', frame > 0)
    success = AiSceneWrite(universe, usdScene, params)
    AiParamValueMapDestroy(params)
    AiUniverseDestroy(universe)

    if not success:
        print('ERROR: Scene export failed on frame %d' % frame)
        AiEnd()
        sys.exit(-1)

AiEnd()

# ---------------------------------------------------------------------------
#  Validate the resulting USD file
# ---------------------------------------------------------------------------
errors = []
with open(usdScene, 'r') as f:
    content = f.read()

# 1) Constant shader attributes must NOT have timeSamples
if 'inputs:base_color.timeSamples' in content:
    errors.append('base_color on late_shader has spurious timeSamples')

if 'inputs:specular_roughness.timeSamples' in content:
    errors.append('specular_roughness on late_shader has spurious timeSamples')

# 2) info:id should be a uniform default, never time-sampled
if 'info:id.timeSamples' in content:
    errors.append('info:id has timeSamples (should be uniform default)')


# 4) The transform for late_object must not contain garbage (denormalized floats)
# 5) late_object must have time samples on primvars:arnold:visibility (invisible before
#    first appearance frame, fully visible after (e.g. 1: 0, 2: 255 when APPEAR_FRAME is 2)
late_obj_start = content.find('def Mesh "late_object"')
if late_obj_start >= 0:
    late_obj_section = content[late_obj_start:content.find('\ndef ', late_obj_start + 1)]
    if re.search(r'[0-9]+\.[0-9]+e-[23][0-9]{2}', late_obj_section):
        errors.append('Transform for late_object contains garbage/denormalized values')

    vis_ts = re.search(
        r'uchar\s+primvars:arnold:visibility\.timeSamples\s*=\s*\{([^}]*)\}',
        late_obj_section,
        re.DOTALL)
    if not vis_ts:
        errors.append(
            'late_object missing uchar primvars:arnold:visibility.timeSamples block')
    else:
        vis_body = vis_ts.group(1)
        if not re.search(r'1:\s*0\b', vis_body) or not re.search(r'2:\s*255\b', vis_body):
            errors.append(
                'late_object primvars:arnold:visibility.timeSamples must include 1: 0 and 2: 255')
else:
    errors.append('def Mesh "late_object" not found')

# 6) Constant attributes on the always-present object must not have timeSamples
always_start = content.find('def Mesh "always_here"')
always_end = content.find('\ndef ', always_start + 1) if always_start >= 0 else -1
if always_start >= 0 and always_end >= 0:
    always_section = content[always_start:always_end]
    if 'doubleSided.timeSamples' in always_section:
        errors.append('Constant attribute doubleSided on always_here has spurious timeSamples')

if errors:
    for e in errors:
        print('FAIL: %s' % e)
    print('\n--- Generated USD file ---')
    print(content)
    sys.exit(-1)

print('SUCCESS')
