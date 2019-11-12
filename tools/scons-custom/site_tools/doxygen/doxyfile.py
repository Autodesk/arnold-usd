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
import os
from fnmatch import fnmatch

import SCons.Action
import SCons.Builder
import SCons.Node

def _action(target, source, env):
    with open(target[0].abspath, 'w') as f:
        for k, v in source[0].value.items():
            if hasattr(v, '__iter__'):
                v = ' '.join(v)
            f.write('{} = {}\n'.format(k, v))
    return None

builder = SCons.Builder.Builder(
    action         = SCons.Action.Action(_action, None),
    target_factory = SCons.Node.FS.File,
    source_factory = SCons.Node.Python.Value,
    single_target  = True,
    single_source  = True,
)

def sources(f, root_dir):
   default_file_patterns = [ '*.cpp', '*.h', '*.md', '*.py' ]

   data = parse(f)

   recursive = data.get('RECURSIVE', 'NO') == 'YES'
   f_pat     = data.get('FILE_PATTERNS', [])
   f_pat     = f_pat if f_pat else default_file_patterns
   f_exc     = data.get('EXCLUDE_PATTERNS', [])
   input     = data.get('INPUT', [])
   input     = input if input else [root_dir]

   sources = []

   for i in input:
      if not os.path.isabs(i):
         i = os.path.join(root_dir, i)
      if os.path.isfile(i):
         sources.append(i)
      elif os.path.isdir(i):
         if recursive:
            for root, dirs, files in os.walk(i):
               for fn in files:
                  included = reduce(lambda x, y: x or  fnmatch(fn, y), f_pat, False) if f_pat else True
                  excluded = reduce(lambda x, y: x and fnmatch(fn, y), f_exc, True ) if f_exc else False
                  if included and not excluded:
                     sources.append(os.path.join(root, fn))
         else:
            for pattern in f_pat:
               sources.extend(glob.glob(os.path.join(i, pattern)))

   # TODO: Process also @INCLUDE and TAGFILES

   return sources

def parse(f):
   """
   Parse a Doxygen source file and return a dictionary of all the values.
   Values will be strings and lists of strings.
   """
   data = {}

   import shlex
   lex = shlex.shlex(instream=f, posix=True)
   lex.wordchars += "*+./-:@"
   lex.whitespace = lex.whitespace.replace("\n", "")
   lex.escape = ""
   
   conf_dir = os.path.dirname(f.name)

   lineno = lex.lineno
   token = lex.get_token()
   key = None
   last_token = ""
   key_token = True  # The first token should be a key.
   next_key = False
   new_data = True

   def append_data(data, key, new_data, token):
      if new_data or len(data[key]) == 0:
         data[key].append(token)
      else:
         data[key][-1] += token

   while token:
      if token in ['\n']:
         if last_token not in ['\\']:
            key_token = True
      elif token in ['\\']:
         pass
      elif key_token:
         key = token
         key_token = False
      else:
         if token == "+=":
            if key not in data:
               data[key] = []
         elif token == "=":
            if key == "TAGFILES" and key in data:
               append_data(data, key, False, "=")
               new_data = False
            elif key == "@INCLUDE" and key in data:
               # don't reset the @INCLUDE list when we see a new @INCLUDE line.
               pass
            else:
               data[key] = []
         elif key == "@INCLUDE":
            # special case for @INCLUDE key: read the referenced
            # file as a doxyfile too.
            nextfile = token
            if not os.path.isabs(nextfile):
               nextfile = os.path.join(conf_dir, nextfile)
            if nextfile in data[key]:
               raise Exception("recursive @INCLUDE in Doxygen config: " + nextfile)
            data[key].append(nextfile)
            fh = open(nextfile, 'r')
            DoxyfileParse(fh.read(), conf_dir, data)
            fh.close()
         else:
            append_data(data, key, new_data, token)
            new_data = True

      last_token = token
      token = lex.get_token()

      if last_token == '\\' and token != '\n':
         new_data = False
         append_data(data, key, new_data, '\\')

   # Compress lists of len 0 and 1 into single strings and keep some tags as lists
   for k, v in data.items():
      keep_list = k in ['INPUT', 'FILE_PATTERNS', 'EXCLUDE_PATTERNS', 'TAGFILES', '@INCLUDE']
      if keep_list    : continue
      elif len(v) == 0: data[k] = ''
      elif len(v) == 1: data[k] = v[0]

   return data
