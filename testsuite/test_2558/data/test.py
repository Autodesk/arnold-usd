import os
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

AiBegin()
universe = AiUniverse()

usdScene = 'scene.usda'
AiSceneLoad(universe, 'scene.ass', None)
AiSceneWrite(universe, usdScene, None)
AiEnd()

expectedLine = 'tangent:indices'
invalidLine = 'tangentidxs'
success = False
with open(usdScene, 'r') as f:
    lines = f.readlines()
    for line in lines:
        if expectedLine in line:
            success = True
        if invalidLine in line:
            success = False
            break
 
if not success:
    sys.exit(-1)

