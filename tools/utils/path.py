# vim: filetype=python
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
import os, shutil

def list(path, extensions=None):
   '''
   Returns a list of all files with an extension from the 'extensions' list
   '''
   path = os.path.normpath(path)
   result = []
   for root, dirs, files in os.walk(path):
      if '.git' in dirs:
         dirs.remove('.git')
      for f in files:
         if not extensions or os.path.splitext(f)[1] in extensions:
            # Build the absolute path and then remove the root path, to get the relative path from root
            result.append(os.path.join(root, f)[len(path) + 1:])
   result.sort(key=os.path.split) ## keep the files in a predictable order
   return result

def remove(path):
   '''
   Handy function to remove files only if they exist
   '''
   if os.path.exists(path):
      if os.path.isdir(path):
         shutil.rmtree(path)
      else:
         os.remove(path)

def copy(src, target):
   '''
   Copies a file or a symbolic link (creating a new link in the target dir)
   '''
   if os.path.isdir(target):
      target = os.path.join(target, os.path.basename(src))

   if os.path.islink(src):
      linked_path = os.readlink(src)
      os.symlink(linked_path, target)
   else:
      shutil.copy(src, target)
