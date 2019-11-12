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
from SCons.Script import *

import os, glob, re

def find_tests_using_node(name, env):
   TEST_GROUP = []
   l = glob.glob(os.path.join('testsuite', env['TEST_PATTERN']))

   for test in l:
      found = False
      d = os.path.join(test, 'data')
      if os.path.isdir(d):
         for f in os.listdir(d):
            path = os.path.join(d, f)
            if os.path.isfile(path):
               ext = os.path.splitext(f)[1]
               expression = {
                  ".ass": "^[ \t]*%s[ \t]*\n[ \t]*{" % name,
                  ".py": "AiNode\([\"\']%s[\"\']\)" % name,
                  ".cpp": "AiNode\(\"%s\"\);" % name
               }.get(ext, None)
               
               if expression:
                  with open(os.path.join(d, f), 'r') as file:
                     if re.search(expression, file.read(), re.MULTILINE):
                        found = True
                        break
      if found:
         TEST_GROUP.append(os.path.basename(test))

   return TEST_GROUP 

## Finds a test group and returns a list of all tests included
def find_test_group(group, env):

   def find_test_group_in_file(group, file):
      TEST_GROUP = set()
      merge = False
      found = False
   
      with open(file, 'r') as f:
         for line in f.readlines():
            line = line.lstrip(' \t')
            if line.startswith('#'):
               # Skip comment lines
               continue
            (l, s, r) = line.partition(':')
            if s == ':':
               name = l.rstrip()
               if name.endswith('+'):
                  merge = True
                  name = name[:-1]
               else:
                  merge = False
               if group == name:
                  # We found the test group
                  found = True
                  for t in Split(r):
                     if t.startswith('test_'):
                        TEST_GROUP.add(t)
                     else:
                        TEST_GROUP = TEST_GROUP.union(find_test_group(t, env))
                  break
      return (TEST_GROUP, found, merge)

   TEST_GROUP = set()
   merge = False
   found = False

   # The special "failed" group is expanded to the list of all failing tests   
   if group == 'failed':
      for dir in glob.glob(os.path.join(env.Dir('.').abspath, 'test_*')):
         file = os.path.join(dir, 'STATUS')
         with open(str(file), 'r') as f:
            value = f.readline().strip('\n')
         if value != 'OK':
            TEST_GROUP.add(os.path.basename(os.path.dirname(str(file))))
   # Special 'all' group is expanded to the whole testsuite, including the 'ignored' tests
   elif group == 'all':
      testlist = glob.glob(os.path.join('testsuite', env['TEST_PATTERN']))
      for name in testlist:
         if os.path.exists(os.path.join(name, 'README')):
            TEST_GROUP.add(os.path.basename(name))
   else:
      # Search the user local file for this group (only if the local file exists)
      if os.path.exists(os.path.join('testsuite', 'groups.local')):
         (TEST_GROUP, found, merge) = find_test_group_in_file(group, os.path.join('testsuite', 'groups.local'))
         
      # If not found, then search the global test groups mapping file
      if merge or not found:
         (TEST_GROUP2, found, merge) = find_test_group_in_file(group, os.path.join('testsuite', 'groups'))
         TEST_GROUP = TEST_GROUP.union(TEST_GROUP2)

      if merge or not found:
         testlist = find_tests_using_node(group, env)
         for name in testlist:
            TEST_GROUP.add(os.path.basename(name))
      
   return TEST_GROUP

## Gets the next test name from the list of existing tests (assuming a 'test_0000' format)
def get_next_test_name():
   l = glob.glob(os.path.join("testsuite", 'test_[0-9][0-9][0-9][0-9]'))
   l.sort()
   result = int(os.path.basename(l[-1])[5:]) + 1
   return "%04d" % result

# Get list of tests included in the given groups, or all tests if no group given
def get_test_list(groups, env, PATTERNS, TAGS):
   tests = []
   if not groups:
      pattern = env['TEST_PATTERN']
      PATTERNS.append(pattern)
      for name in glob.glob(os.path.join('testsuite', pattern)):
         tests.append(os.path.join(os.path.basename(name)))
   else:
      tags = groups.split(',')
      for tag in tags:
         if tag.startswith('test_'):
            PATTERNS.append(tag)
            for name in glob.glob(os.path.join('testsuite', tag)):
               tests.append(os.path.join(os.path.basename(name)))
         else:
            TAGS.append(tag)
            test_group = find_test_group(tag, env)
            if len(test_group) == 0:
               print "WARNING: No tests related to tag \"%s\"" % tag
            else:
               for test in test_group:
                  tests.append(test)

   return tests
