import os
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

usdScene = 'test_resaved.usda'

# ---------------------------------------------------------------------------
#  Build an arnold scene with a uv_camera whose ray_origin / ray_direction
#  are linked to shaders, and export it to USD.
# ---------------------------------------------------------------------------
AiBegin()
universe = AiUniverse()

camera = AiNode(universe, 'uv_camera', '/cam')

ray_origin_shader = AiNode(universe, 'noise', '/shaders/ray_origin_shader')
ray_direction_shader = AiNode(universe, 'noise', '/shaders/ray_direction_shader')

AiNodeLink(ray_origin_shader, 'ray_origin', camera)
AiNodeLink(ray_direction_shader, 'ray_direction', camera)

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
#  Validate the authored USD: an ArnoldNodeGraph whose output terminals are
#  connected to the shaders, and the camera attributes connected to the graph.
# ---------------------------------------------------------------------------
errors = []
with open(usdScene, 'r') as f:
    content = f.read()

def require(snippet):
    if snippet not in content:
        errors.append('missing in USD: %s' % snippet)

# The camera references its shaders through an ArnoldNodeGraph
require('def ArnoldNodeGraph "camera_shaders"')
# The node graph output terminals are connected to the shaders
require('token outputs:ray_origin.connect')
require('token outputs:ray_direction.connect')
# The camera attributes connect to the node graph output terminals
require('primvars:arnold:ray_origin')
require('.outputs:ray_origin>')
require('primvars:arnold:ray_direction')
require('.outputs:ray_direction>')

if errors:
    for e in errors:
        print('FAIL: %s' % e)
    print('\n--- Generated USD file ---')
    print(content)
    sys.exit(-1)

# ---------------------------------------------------------------------------
#  Round-trip: load the USD back and check the shader links are restored.
# ---------------------------------------------------------------------------
AiBegin()
universe = AiUniverse()
AiSceneLoad(universe, usdScene, None)

cam = AiNodeLookUpByName(universe, '/cam')
if cam is None:
    print('FAIL: uv_camera /cam not found after reload')
    AiEnd()
    sys.exit(-1)

for param in ('ray_origin', 'ray_direction'):
    if not AiNodeIsLinked(cam, param):
        errors.append('%s is not linked on the uv_camera after reload' % param)

AiUniverseDestroy(universe)
AiEnd()

if errors:
    for e in errors:
        print('FAIL: %s' % e)
    sys.exit(-1)

print('SUCCESS')
