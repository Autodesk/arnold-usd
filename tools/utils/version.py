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

import itertools
import operator

from .misc import is_string

class Version:

   # Constructor
   def __init__(self, other = None, t = None, s = '.'):
      self.v = [] # Version number X.Y.Z... as a list [X,Y,Z,...]
      self.t = t  # Version tag. As in X.Y.Z.tag or X.Y.Z-tag
      self.s = s  # Separator between Version number and Version tag
      if isinstance(other, Version):
         self.v = list(other.v)
      elif isinstance(other, (list, tuple)):
         self.v = [int(x) for x in other]
      elif is_string(other):
         self.v = [int(x) for x in other.split('.')]
      elif other is not None:
         raise ValueError

   # printable representation
   def __repr__(self):
      return '.'.join([('%u' % x) for x in self.v]) + ((self.s + self.t) if self.t else '')

   # nesting degree of the version
   # 4 = 1, 4.0 = 2, 4.0.0 = 3, etc.
   def __len__(self):
      return len(self.v)

   # [] operators for set and get
   def __getitem__(self, i):
      if isinstance(i, slice):
         return Version([x for x in itertools.islice(self.v, i.start, i.stop, i.step)])
      elif i < len(self.v):
         return self.v[i]
      else:
         raise IndexError
   def __setitem__(self, i, e):
      L = len(self.v)
      if i >= L:
         self.v = self.v + [0] * (i + 1 - L)
      self.v[i] = e

   # this is a private method used by the comparison operators
   def __compare(self, other, func):
      if not isinstance(other, Version):
         other = Version(other)
      Ln, Rn = len(self.v), len(other.v)
      n = max(Ln, Rn)
      L, R = self.v + [0] * (n - Ln), other.v + [0] * (n - Rn)
      return func(L, R)

   # Comparison operators
   def __lt__(self, other): return self.__compare(other, operator.lt)
   def __le__(self, other): return self.__compare(other, operator.le)
   def __eq__(self, other): return self.__compare(other, operator.eq)
   def __ne__(self, other): return self.__compare(other, operator.ne)
   def __ge__(self, other): return self.__compare(other, operator.ge)
   def __gt__(self, other): return self.__compare(other, operator.gt)
