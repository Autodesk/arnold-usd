import os
import re
import shutil
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

# "/Group" pre-exists on the target stage exactly as authored in existing.usda,
# standing in for a prim written by another tool (e.g. mayaUsd's own native
# -defaultCameras export) with no matching Arnold node of the same name.
ANCESTOR_TRANSLATE = (5.0, 0.0, 1.0)

# The node's own *world* matrix, as any Arnold shape/camera always carries an
# absolute transform regardless of USD hierarchy. This is the correct, final
# world position we expect to see once "/Group"'s transform above it is
# properly cancelled out.
EXPECTED_WORLD_TRANSLATE = (2.0, 3.0, -4.0)

usd_scene = 'test_appended.usda'
shutil.copyfile('existing.usda', usd_scene)

AiBegin()
universe = AiUniverse()

mesh = AiNode(universe, 'polymesh', '/Group/Child')
AiNodeSetArray(mesh, 'vlist', AiArray(8, 1, AI_TYPE_VECTOR,
    AtVector(-1, -1, -1), AtVector(1, -1, -1),
    AtVector(-1,  1, -1), AtVector(1,  1, -1),
    AtVector(-1, -1,  1), AtVector(1, -1,  1),
    AtVector(-1,  1,  1), AtVector(1,  1,  1)))
AiNodeSetArray(mesh, 'nsides', AiArray(6, 1, AI_TYPE_UINT, 4, 4, 4, 4, 4, 4))
AiNodeSetArray(mesh, 'vidxs', AiArray(24, 1, AI_TYPE_UINT,
    0, 1, 3, 2,  4, 6, 7, 5,  0, 4, 5, 1,
    2, 3, 7, 6,  0, 2, 6, 4,  1, 5, 7, 3))
AiNodeSetMatrix(mesh, 'matrix', AtMatrix(
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    EXPECTED_WORLD_TRANSLATE[0], EXPECTED_WORLD_TRANSLATE[1], EXPECTED_WORLD_TRANSLATE[2], 1))

params = AiParamValueMap()
AiParamValueMapSetBool(params, 'binary', False)
AiParamValueMapSetBool(params, 'append', True)
success = AiSceneWrite(universe, usd_scene, params)
AiParamValueMapDestroy(params)
AiUniverseDestroy(universe)
AiEnd()

if not success:
    print('ERROR: Scene export failed')
    sys.exit(-1)

with open(usd_scene, 'r') as f:
    content = f.read()

child_start = content.find('def Mesh "Child"')
if child_start < 0:
    print('FAIL: def Mesh "Child" not found in %s' % usd_scene)
    print(content)
    sys.exit(-1)

# The next "def " at the same or lower indentation starts the following prim;
# a plain find from child_start onward is precise enough here since this
# stage only has the one mesh prim.
child_section = content[child_start:]

m = re.search(
    r'matrix4d\s+xformOp:transform\s*=\s*\(\s*'
    r'\(([^)]*)\)\s*,\s*\(([^)]*)\)\s*,\s*\(([^)]*)\)\s*,\s*\(([^)]*)\)\s*\)',
    child_section)
if not m:
    print('FAIL: no matrix4d xformOp:transform authored on "Child" - nothing '
          'to cancel the ancestor transform against (MTOA-3064 regression)')
    print(content)
    sys.exit(-1)

rows = [[float(v) for v in m.group(i).split(',')] for i in range(1, 5)]


def mat_mult(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


# Compose the child's authored local matrix with the ancestor's own authored
# transform exactly like USD does when consuming the stage (row-vector
# convention: world = local * parent).
ancestor = [
    [1, 0, 0, 0],
    [0, 1, 0, 0],
    [0, 0, 1, 0],
    [ANCESTOR_TRANSLATE[0], ANCESTOR_TRANSLATE[1], ANCESTOR_TRANSLATE[2], 1],
]
world = mat_mult(rows, ancestor)
world_translate = tuple(world[3][:3])

tolerance = 1e-4
if any(abs(world_translate[i] - EXPECTED_WORLD_TRANSLATE[i]) > tolerance for i in range(3)):
    print('FAIL: composed world translate %s does not match expected %s - the '
          "child's local matrix was not cancelled against the ancestor's "
          'authored transform (MTOA-3064 regression)' % (world_translate, EXPECTED_WORLD_TRANSLATE))
    sys.exit(-1)

print('SUCCESS')
