import os
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

# Load a scene where a step-size volume mesh is bound to a material carrying both
# an arnold:volume terminal (standard_volume) and a universal surface terminal
# (UsdPreviewSurface). The reader must prefer the volume terminal for the volume
# shape, so the mesh ends up with the standard_volume as its shader - not the
# preview surface (which would render the volume black). See MAXTOA-2033.

AiBegin()
universe = AiUniverse()
AiSceneLoad(universe, 'test.usda', None)

# Find the polymesh (the implicit volume)
mesh = None
it = AiUniverseGetNodeIterator(universe, AI_NODE_SHAPE)
while not AiNodeIteratorFinished(it):
    n = AiNodeIteratorGetNext(it)
    if AiNodeIs(n, 'polymesh'):
        mesh = n
        break
AiNodeIteratorDestroy(it)

errors = []
if mesh is None:
    errors.append('no polymesh found in the scene')
else:
    shader = AiNodeGetPtr(mesh, 'shader')
    if not shader:
        errors.append('no shader bound to the volume mesh')
    else:
        entry = AiNodeEntryGetName(AiNodeGetNodeEntry(shader))
        if entry != 'standard_volume':
            errors.append('expected standard_volume bound to the volume mesh, got "%s" (%s)'
                          % (entry, AiNodeGetName(shader)))

AiUniverseDestroy(universe)
AiEnd()

if errors:
    for e in errors:
        print('FAIL: %s' % e)
    sys.exit(-1)

print('SUCCESS')
