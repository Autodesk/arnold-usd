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

    # This mesh is bound to a UsdPreviewSurface material. The arnold top shader has
    # another name (e.g. an aiSurfaceShader override) but shares the file texture,
    # which collides with the UsdUVTexture prim
    file1 = AiNode(universe, 'image', '/file1')
    AiNodeSetStr(file1, 'filename', 'tex.png')
    previewShader = AiNode(universe, 'standard_surface', '/aiStandardSurface1')
    AiNodeLink(file1, 'base_color', previewShader)
    preview = AiNode(universe, 'polymesh', '/geo/preview')
    AiNodeSetArray(preview, 'vlist', AiArray(4, 1, AI_TYPE_VECTOR,
        AtVector(-1, -1, 2), AtVector(1, -1, 2), AtVector(1, 1, 2), AtVector(-1, 1, 2)))
    AiNodeSetArray(preview, 'nsides', AiArray(1, 1, AI_TYPE_UINT, 4))
    AiNodeSetArray(preview, 'vidxs', AiArray(4, 1, AI_TYPE_UINT, 0, 1, 2, 3))
    AiNodeSetPtr(preview, 'shader', previewShader)

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

# Read the file back: the arnold shading trees must be found through the
# node graph outputs
universe = AiUniverse()
AiSceneLoad(universe, usdScene, None)
readErrors = []
for meshName, shaderEntry, linkedEntry in [('/geo/mesh', 'standard_surface', 'image'),
                                           ('/geo/preview', 'standard_surface', 'image')]:
    readMesh = AiNodeLookUpByName(universe, meshName)
    readShader = AiNodeGetPtr(readMesh, 'shader') if readMesh else None
    if not readShader:
        readErrors.append('%s: no shader after reading the file back' % meshName)
        continue
    entry = AiNodeEntryGetName(AiNodeGetNodeEntry(readShader))
    link = AiNodeGetLink(readShader, 'base_color')
    linkEntry = AiNodeEntryGetName(AiNodeGetNodeEntry(link)) if link else None
    if entry != shaderEntry or linkEntry != linkedEntry:
        readErrors.append('%s: expected %s <- %s after reading the file back, got %s (%s) <- %s'
                          % (meshName, shaderEntry, linkedEntry, entry, AiNodeGetName(readShader), linkEntry))
AiUniverseDestroy(universe)

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

errors = readErrors

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
# and the material output goes through a node graph output (USD encapsulation)
if 'outputs:arnold:surface.connect = </mtl/mySG/arnold.outputs:surface>' not in content:
    errors.append('outputs:arnold:surface is not connected to the arnold node graph output')
if 'outputs:surface.connect = </mtl/mySG/arnold/myShader.outputs:out>' not in content:
    errors.append('the arnold node graph output is not connected to the arnold shader')

# 3) A material where only a texture name collides also uses the node graph
checkId('/mtl/previewSG/usdPreviewSurface1', 'UsdPreviewSurface')
checkId('/mtl/previewSG/file1', 'UsdUVTexture')
checkId('/mtl/previewSG/arnold/aiStandardSurface1', 'arnold:standard_surface')
checkId('/mtl/previewSG/arnold/file1', 'arnold:image')
if 'outputs:arnold:surface.connect = </mtl/previewSG/arnold.outputs:surface>' not in content:
    errors.append('previewSG outputs:arnold:surface is not connected to the arnold node graph output')

# 4) No input ends up with several connection sources
if re.search(r'\.connect = \[', content):
    errors.append('an attribute has several connection sources')

# 5) The material created by the writer keeps its shaders directly under it
controlPaths = [p for p, i in shaderIds.items() if i == 'arnold:standard_surface' and p.endswith('/controlShader')]
if len(controlPaths) != 1:
    errors.append('expected one controlShader prim, got %s' % controlPaths)
nodeGraphs = sorted(p for p, t in primTypes.items() if t == 'NodeGraph' and p.endswith('/arnold'))
if nodeGraphs != ['/mtl/mySG/arnold', '/mtl/previewSG/arnold']:
    errors.append('unexpected arnold node graphs: %s' % nodeGraphs)

if errors:
    for e in errors:
        print('FAIL: %s' % e)
    print('\n--- Generated USD file ---')
    print(content)
    sys.exit(-1)

print('SUCCESS')
