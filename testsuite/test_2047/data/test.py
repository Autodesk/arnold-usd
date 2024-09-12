import os
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

AiBegin()
universe = AiUniverse()

usdScene = 'shaders_resaved.usda'
AiSceneLoad(universe, 'shaders.ass', None)
params = AiParamValueMap()
AiParamValueMapSetInt(params, 'mask', AI_NODE_SHADER)
AiSceneWrite(universe, usdScene, params)
AiEnd()

expectedLines = ['def Scope "mtl"',
				'def Material "aiStandard5SG"',
				'token outputs:arnold:displacement.connect = </displacementShader6.outputs:out>',
				'token outputs:arnold:surface.connect = </aiStandard5.outputs:out>',
				'def Material "aiStandard3SG"',
				'token outputs:arnold:displacement.connect = </displacementShader4.outputs:out>',
				'token outputs:arnold:surface.connect = </aiStandard3.outputs:out>',
				'def Material "aiStandard4SG"',
				'token outputs:arnold:displacement.connect = </displacementShader5.outputs:out>',
				'token outputs:arnold:surface.connect = </aiStandard4.outputs:out>']

expectedLinesCount = len(expectedLines)
currentLine = 0

success = False
with open(usdScene, 'r') as f:
    lines = f.readlines()
    for line in lines:
        if expectedLines[currentLine] in line:
            currentLine = currentLine + 1
            if currentLine == expectedLinesCount:
                success = True
                break

if not success:
    print('Line not found in the output usd file:')
    print(expectedLines[currentLine])
    sys.exit(-1)

print('SUCCESS')