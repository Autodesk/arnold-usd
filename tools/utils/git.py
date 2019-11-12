# vim: filetype=python
# Copyright 2019 Autodesk, Inc.
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
from . import system
from .version import Version

def check():
   '''
   Check for Git existence
   '''
   cmd = ['git', '--version']
   r, o = system.execute(cmd)
   return r == 0

def version():
   '''
   Return the Git version
   '''
   cmd = ['git', '--version']
   r, o = system.execute(cmd)
   print r
   return Version(o[0].split()[-1])

def uncommitted():
   '''
   Check for uncommitted changes in the index
   '''
   cmd = ['git', 'diff', '--quiet', '--ignore-submodules', '--cached']
   r, o = system.execute(cmd)
   return r != 0

def unstaged():
   '''
   Check for unstaged changes
   '''
   cmd = ['git', 'diff-files', '--quiet', '--ignore-submodules']
   r, o = system.execute(cmd)
   return r != 0

def untracked():
   '''
   Check for untracked files
   '''
   cmd = ['git', 'ls-files', '--others', '--exclude-standard']
   r, o = system.execute(cmd)
   return len(o) > 0 if r == 0 else False

def stashed():
   '''
   Check for stashed changes
   '''
   return sha1('refs/stash') is not None

def branch():
   '''
   Return the short symbolic-ref of the branch where the HEAD is attached. If
   the HEAD is dettached, return None
   '''
   cmd = ['git', 'symbolic-ref', '--short', '-q', 'HEAD']
   r, o = system.execute(cmd)
   return o[0] if r == 0 else None

def remote_url(remote='origin'):
   cmd = ['git', 'config', '--get', 'remote.{}.url'.format(remote)]
   r, o = system.execute(cmd)
   return o[0] if r == 0 else None

def sha1(obj='HEAD'):
   cmd = ['git', 'rev-parse', '--verify', obj]
   r, o = system.execute(cmd)
   return o[0] if r == 0 else None

def tags(obj='HEAD'):
   '''
   Return the list of tags that are pointing to "obj"
   '''
   cmd = ['git', 'tag', '--points-at', sha1(obj)]
   r, o = system.execute(cmd)
   return o if r == 0 else []

def parents(obj='HEAD'):
   '''
   Return the list of "obj"'s parent commits
   '''
   cmd = ['git', 'rev-list', '--parents', '-n 1', obj]
   r, o = system.execute(cmd)
   return o[0].split()[1:] if r == 0 else []

def children(obj='HEAD'):
   '''
   Return the list of "obj"'s children commits
   '''
   cmd = ['git', 'rev-list', '--parents', '--all']
   r, o = system.execute(cmd)
   if r == 0:
      # Split every line in "line_s" and group by (commit, parent commits) in
      # "line_g"
      lines_s = (x.split() for x in o)
      lines_g = ((x[0], x[1:]) for x in lines_s)
      commit_sha1 = sha1(obj)
      # "commit" will be a child if "commit_sha1" is among the parents
      return [commit for commit, parents in lines_g if commit_sha1 in parents]
   else:
      return []
