import os
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

AiBegin()
universe = AiUniverse()

AiSceneLoad(universe, 'scene.ass', None)
params = AiParamValueMap()
AiSceneWrite(universe, 'test_resaved.usda', params)
AiParamValueMapDestroy(params)
AiUniverseDestroy(universe)

############################
universe = AiUniverse()
AiSceneLoad(universe, 'test_resaved.usda', None)
renderSession = AiRenderSession(universe)
AiRender(renderSession)

AiRenderSessionDestroy(renderSession)
AiUniverseDestroy(universe)

#############################

src_file_path = 'testrender.exr'
if not os.path.exists(src_file_path):
    sys.exit(-1)

oiiotool_path = os.path.join(os.environ['ARNOLD_BINARIES'], 'oiiotool')
cmd = '{} testrender.exr -ch "RGBA_grp1.R,RGBA_grp1.G,RGBA_grp1.B,RGBA_grp1.A" -o testrender.tif'.format(oiiotool_path)
os.system(cmd)

AiEnd()
