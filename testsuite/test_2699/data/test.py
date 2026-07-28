import os
import sys

# Regression test for arnold-usd #2699.
#
# An asset authors a constant primvars:crypto_asset on a parent prim, inherited by the
# descendant geometry and resolved as the Cryptomatte asset id. When the asset is added
# through an instanceable reference, the inherited primvar used to reach the shape (in the
# Hydra render-delegate passes) as a single-element per-face "uniform" primvar. It was then
# declared as a size-1 uniform array, which Arnold cannot index per face, producing
# "out-of-range error (index N of 1)" and dropping the Cryptomatte asset data.
#
# The Cryptomatte asset ids are name hashes that differ between the reading paths, so an
# image comparison is not stable here. Instead we assert that the render does not emit the
# out-of-range / failed-lookup errors. This runs once per pass (usd / hydra / hydra2) with
# the pass-specific environment, so it guards the render-delegate paths where the bug lived.

kick_path = os.path.join(os.environ['ARNOLD_BINARIES'], 'kick')
cmd = '{} test.usda -o testrender.exr -dw -dp -v2 2>&1'.format(kick_path)
print(cmd)
output = os.popen(cmd).read()
print(output)

errors = [
    'out-of-range error',
    "attempting to get uniform parameter 'crypto_asset",
]
found = [e for e in errors if e in output]
if found:
    print('Failure! Inherited primvars:crypto_asset on an instanceable reference produced '
          'errors (#2699): {}'.format(found))
    sys.exit(-1)

print('Success! No out-of-range / failed crypto_asset lookup for the instanceable reference.')
