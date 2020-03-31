import os
import os.path

cmd = os.path.join(os.getenv('PREFIX_BIN'), 'arnold_to_usd')
cmd += ' scene.ass scene.usda'

print('Running: {}'.format(cmd))
print(cmd)
os.system(cmd)

cmd = os.path.join(os.getenv('ARNOLD_BINARIES'), 'kick')
cmd += ' test.ass -dw -r 160 120 -bs 16'
print('Running: {}'.format(cmd))
os.system(cmd)