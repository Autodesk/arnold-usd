import os
import re
import shutil
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

# mayausd.usda holds a mesh bound to a MaterialX material, whose shaders are
# named like the arnold shaders written below
usdScene = 'test_resaved.usda'
shutil.copyfile('mayausd.usda', usdScene)

AiBegin()

for frame in range(2):
    universe = AiUniverse()

    image = AiNode(universe, 'image', '/myTex')
    AiNodeSetStr(image, 'filename', 'tex.png')
    shader = AiNode(universe, 'standard_surface', '/myShader')
    AiNodeLink(image, 'base_color', shader)

    mesh = AiNode(universe, 'polymesh', '/geo/mesh')
    AiNodeSetArray(mesh, 'vlist', AiArray(4, 1, AI_TYPE_VECTOR,
        AtVector(-1, -1, 0), AtVector(1, -1, 0), AtVector(1, 1, 0), AtVector(-1, 1, 0)))
    AiNodeSetArray(mesh, 'nsides', AiArray(1, 1, AI_TYPE_UINT, 4))
    AiNodeSetArray(mesh, 'vidxs', AiArray(4, 1, AI_TYPE_UINT, 0, 1, 2, 3))
    AiNodeSetPtr(mesh, 'shader', shader)

    # This mesh has no material in mayausd.usda, the writer creates one
    controlShader = AiNode(universe, 'standard_surface', '/controlShader')
    control = AiNode(universe, 'polymesh', '/geo/control')
    AiNodeSetArray(control, 'vlist', AiArray(4, 1, AI_TYPE_VECTOR,
        AtVector(-1, -1, 1), AtVector(1, -1, 1), AtVector(1, 1, 1), AtVector(-1, 1, 1)))
    AiNodeSetArray(control, 'nsides', AiArray(1, 1, AI_TYPE_UINT, 4))
    AiNodeSetArray(control, 'vidxs', AiArray(4, 1, AI_TYPE_UINT, 0, 1, 2, 3))
    AiNodeSetPtr(control, 'shader', controlShader)

    params = AiParamValueMap()
    AiParamValueMapSetBool(params, 'binary', False)
    AiParamValueMapSetFlt(params, 'frame', float(frame))
    AiParamValueMapSetBool(params, 'append', True)
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
with open(usdScene, 'r') as f:
    content = f.read()

# Collect the type and info:id of every prim, from the usda prim blocks
primTypes = {}
shaderIds = {}
stack = []
pending = None
for line in content.splitlines():
    stripped = line.strip()
    match = re.match(r'(?:def|over)\s+(\w+\s+)?"(\w+)"', stripped)
    if match:
        pending = (match.group(2), (match.group(1) or '').strip())
        continue
    if stripped == '{' and pending:
        stack.append(pending[0])
        path = '/' + '/'.join(stack)
        primTypes[path] = pending[1]
        pending = None
        continue
    if stripped.endswith('{'):
        stack.append(None)
        continue
    if stripped.startswith('}'):
        if stack:
            stack.pop()
        continue
    match = re.match(r'uniform token info:id = "([^"]+)"', stripped)
    if match and stack and stack[-1] is not None:
        shaderIds['/' + '/'.join(stack)] = match.group(1)

errors = []

def checkId(path, expected):
    if shaderIds.get(path) != expected:
        errors.append('%s: expected info:id %s, got %s' % (path, expected, shaderIds.get(path)))

# 1) The MaterialX shaders are untouched
checkId('/mtl/mySG/myShader', 'ND_open_pbr_surface_surfaceshader')
checkId('/mtl/mySG/MayaNG_mySG/myTex', 'ND_image_color3')
if 'outputs:mtlx:surface.connect = </mtl/mySG/myShader.outputs:out>' not in content:
    errors.append('outputs:mtlx:surface no longer points at the MaterialX shader')

# 2) The arnold shading tree is in an "arnold" node graph under the material
if primTypes.get('/mtl/mySG/arnold') != 'NodeGraph':
    errors.append('/mtl/mySG/arnold is not a NodeGraph')
checkId('/mtl/mySG/arnold/myShader', 'arnold:standard_surface')
checkId('/mtl/mySG/arnold/myTex', 'arnold:image')
if 'outputs:arnold:surface.connect = </mtl/mySG/arnold/myShader.outputs:out>' not in content:
    errors.append('outputs:arnold:surface is not connected to the arnold shader')

# 3) No input ends up with several connection sources
if re.search(r'\.connect = \[', content):
    errors.append('an attribute has several connection sources')

# 4) The material created by the writer keeps its shaders directly under it
controlPaths = [p for p, i in shaderIds.items() if i == 'arnold:standard_surface' and p.endswith('/controlShader')]
if len(controlPaths) != 1:
    errors.append('expected one controlShader prim, got %s' % controlPaths)
nodeGraphs = [p for p, t in primTypes.items() if t == 'NodeGraph' and p.endswith('/arnold')]
if nodeGraphs != ['/mtl/mySG/arnold']:
    errors.append('unexpected arnold node graphs: %s' % nodeGraphs)

if errors:
    for e in errors:
        print('FAIL: %s' % e)
    print('\n--- Generated USD file ---')
    print(content)
    sys.exit(-1)

print('SUCCESS')
