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
import operator

class Version:

   # Constructor
   def __init__(self, v, t = None, s = '.'):
      self.__v = [] # Version number X.Y.Z... as a list [X,Y,Z,...]
      self.__t = t  # Version tag. As in X.Y.Z.tag or X.Y.Z-tag
      self.__s = s  # Separator between Version number and Version tag
      if isinstance(v, Version):
         self.__v = v
      elif (type(v) == list) and ([type(x) for x in v] == [int] * len(v)):
         self.__v = v
      elif (type(v) == str):
         try: self.__v = [int(x) for x in v.split('.')]
         except ValueError: pass

   # printable representation
   def __repr__(self):
      return '.'.join([('%u' % x) for x in self.__v]) + (self.__t and (self.__s + self.__t) or '')

   # nesting degree of the version
   # 4 = 1, 4.0 = 2, 4.0.0 = 3, etc.
   def __len__(self): return len(self.__v)

   # [] operators for set and get
   def __getitem__(self, i):    return self.__v[i]
   def __setitem__(self, i, e): self.__v[i] = e

   # this is a private method used by the comparison operators
   def __compare(self, other, func):
      n = max(len(self), len(other))
      L =  self.__v + [0] * (n - len(self) )
      R = other.__v + [0] * (n - len(other))
      return func(L, R)

   # Comparison operators
   def __lt__(self, other): return self.__compare(other, operator.lt)
   def __le__(self, other): return self.__compare(other, operator.le)
   def __eq__(self, other): return self.__compare(other, operator.eq)
   def __ne__(self, other): return self.__compare(other, operator.ne)
   def __ge__(self, other): return self.__compare(other, operator.ge)
   def __gt__(self, other): return self.__compare(other, operator.gt)
