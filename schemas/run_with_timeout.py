"""Run a command under a hard timeout, cleaning up stale output on failure.

Schema generation shells out to hython (Houdini licensing) and to Arnold's
AiBegin() (Arnold licensing). If the license server is unreachable, either of
those can hang indefinitely instead of failing, which would hang the build
with no way to recover short of manually killing processes. Running the
command through this wrapper turns that hang into a clean build failure.

createSchemaFile.py writes its outputs (schema.usda, wrapModule.cpp)
progressively via plain open().write() calls, not atomically via a temp file
plus rename. If the wrapped command is killed mid-write, it can leave a
truncated file on disk with a fresh mtime, which a build system's staleness
check would treat as up to date and skip regenerating on the next build. So on
timeout, this wrapper deletes the caller-specified cleanup paths to force
regeneration next time.
"""

import argparse
import os
import subprocess
import sys


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, required=True)
    parser.add_argument('--cleanup', action='append', default=[])
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)

    command = args.command
    if command and command[0] == '--':
        command = command[1:]

    try:
        completed = subprocess.run(command, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        for path in args.cleanup:
            try:
                os.remove(path)
            except OSError:
                pass
        sys.stderr.write('run_with_timeout: timed out after {:.0f}s running {}\n'.format(
            args.timeout, ' '.join(command)))
        return 1

    return completed.returncode


if __name__ == '__main__':
    sys.exit(main())
