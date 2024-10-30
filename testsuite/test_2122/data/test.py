import os
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

AiBegin()
universe = AiUniverse()

usdScene = 'light_resaved.usda'
AiSceneLoad(universe, 'light.ass', None)
params = AiParamValueMap()
AiParamValueMapSetInt(params, 'mask', AI_NODE_LIGHT)
AiSceneWrite(universe, usdScene, params)
AiEnd()

expectedLines = ['def RectLight "aiAreaLight"',
				'float inputs:height = 2',
				'float inputs:width = 2']
forbiddenLines = ['vector3f[] primvars:arnold:vertices =']

currentLine = 0
expectedLinesCount = len(expectedLines)
success = False
with open(usdScene, 'r') as f:
    lines = f.readlines()
    for line in lines:

        if currentLine < expectedLinesCount and expectedLines[currentLine] in line:
            currentLine = currentLine + 1
            if currentLine == expectedLinesCount:
                success = True
        
        for forbiddenLine in forbiddenLines:
            if forbiddenLine in line:
                print('This attribute should not be authored: {}'.format(line))
                sys.exit(-1)            

if not success:
    print('Line not found in the output usd file:')
    print(expectedLines[currentLine])
    sys.exit(-1)

print('SUCCESS')