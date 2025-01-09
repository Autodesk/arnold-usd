import os

from . import system
import traceback

def detect():
    # Detect devtoolset (DTS) and GCC Toolset installations in linux
    # and return the list sorted by the descending version
    ts_detected = []
    if system.is_linux:
        ts_valid = ['devtoolset', 'gcc-toolset']
        ts_root = os.path.join('/', 'opt', 'rh')
        if os.path.exists(ts_root):
            for ts_dir in os.listdir(ts_root):
                if not any(ts_dir.startswith('{}-'.format(ts_name)) for ts_name in ts_valid):
                    continue
                tokens = ts_dir.rsplit('-', 1)
                ts_name, ts_version = tokens[0], int(tokens[1])
                ts_path = os.path.join(ts_root, f'{ts_name}-{ts_version}', 'root', 'usr', 'bin')
                if not os.path.isdir(ts_path):
                    continue
                ts_detected.append((ts_name, ts_version, ts_path))
    return sorted(ts_detected, key=lambda ts: ts[1], reverse=True)
