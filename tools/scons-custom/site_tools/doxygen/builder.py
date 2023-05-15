# Copyright 2022 Autodesk, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import os

import SCons.Action
import SCons.Builder
import SCons.Errors
import SCons.Node
import SCons.Scanner
import SCons.Script

from . import doxyfile

## load our own python modules
import utils as sa

def _scanner(node, env, path):
    with open(node.abspath, 'r') as f:
        sources = doxyfile.sources(f, env['ROOT_DIR'])
    return sources

def _scan_check(node, env):
    # Should we scan this node for dependencies?
    return os.path.isfile(node.abspath)

def _emitter(target, source, env):
    # Create a local construction environment for just adding an internal builder
    # which generates a Doxyfile from a python dictionary with tags and values
    local_env = env
    local_env['BUILDERS']['Doxyfile'] = doxyfile.builder
    # Parse the source Doxyfile, returning a dictionary with tags and values ...
    old_doxyfile = source[0].abspath
    with open(old_doxyfile, 'r') as f:
        tags = doxyfile.parse(f)
    # ... and override with the dictionary provided in env['DOXYGEN_TAGS']
    tags.update(env.get('DOXYGEN_TAGS', {}))
    # Build the new Doxyfile file from the updated dictionary (and properly
    # defining the dependencies)
    tags_value = SCons.Node.Python.Value(tags)
    env.Depends(tags_value, source)
    new_doxyfile = os.path.join(env['BUILD_BASE_DIR'], 'Doxyfile')
    local_env.Doxyfile(new_doxyfile, tags_value)
    # We set the doxygen target to the output directory.
    new_target = env.Dir(os.path.join(target[0].abspath, 'html'))
    return [new_target], [new_doxyfile]

def _action(target, source, env):
    # Extract some tags for adding them to the execution environment
    with open(source[0].abspath, 'r') as f:
        tags = doxyfile.parse(f)
    env_tags = {k: tags[k] for k in ['PROJECT_NAME', 'PROJECT_NUMBER']}
    # Transfer the system PATH to the execution environment
    env_tags['PATH'] = os.environ['PATH']
    # Execute doxygen with the source Doxyfile
    r, o = sa.system.execute('doxygen {}'.format(source[0].abspath), env=env_tags)
    if r:
        raise SCons.Errors.UserError('[Errno {}] doxygen: {}'.format(r, '\n'.join(o)))
    print('file://{}'.format(target[0].abspath))
    return None

def generate(env):
    # Detect Doxygen and get the version
    r, o = sa.system.execute('doxygen --version')
    if r:
        return
    # Message when executing the Doxygen builder
    action_message = 'Building SDK documentation with Doxygen {} ...'.format(o[0])
    # Add the Doxygen builder to the construction environment
    env['BUILDERS']['Doxygen'] = SCons.Builder.Builder(
        action         = SCons.Action.Action(_action, action_message),
        emitter        = _emitter,
        target_factory = SCons.Node.FS.Entry,
        source_factory = SCons.Node.FS.File,
        single_target  = True,
        single_source  = True,
        source_scanner = SCons.Scanner.Scanner(
            function    = _scanner,
            scan_check  = _scan_check,
        )
    )

def exists(env):
    return env.Detect('doxygen')
