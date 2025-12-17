import os
import sys

sys.path.append(os.path.join(os.environ['ARNOLD_PATH'], 'python'))
from arnold import *

#############################

kick_path = os.path.join(os.environ['ARNOLD_BINARIES'], 'kick')
cmd = '{} scene.usda -o testrender.tif -dw -dp -v 6'.format(kick_path)
print('Run kick with -v6 : ')
print(cmd)
res = os.system(cmd)
if res != 0:
    print('render failed')
    sys.exit(-1)

result = os.popen(cmd).read()
if '| ray counts  ' in result:
    print('Success! -v 6 was taken into account')
else:
    print('Failure! -v was ignored')
    sys.exit(-1)



