import os
import re

class Groups(object):
   def __init__(self, path=None):
      self.load(path)

   def load(self, path):
      groups = {}
      path = os.path.abspath(path) if path else path
      if path and os.path.exists(path) and os.path.isfile(path):
         with open(path, 'r') as f:
            for line in f.readlines():
               line = line.strip()
               if line.startswith('#'):
                  # skip comments
                  continue
               group, separator, tokens = line.partition(':')
               if not separator:
                  # skip lines without a group name
                  continue
               group = group.strip()
               merge = group.endswith('+')
               if merge:
                  group = group[:-1]
               tokens = set(tokens.split())
               if group not in groups:
                  groups[group]  = tokens
               else:
                  groups[group] |= tokens
      self.path   = path
      self.groups = groups

   def get(self):
      '''Returns the list of loaded groups'''
      return self.groups.keys()

   def get_tests(self, group):
      '''Returns the expanded list of tests in a group'''
      tests = set()
      tokens_group = []
      tokens_test  = []
      # Split the list of tokens in two lists (groups/tests)
      for token in self.groups.get(group, []):
         (tokens_group, tokens_test)[token.startswith('test_')].append(token)
      tests |= set(tokens_test)
      # If the group had tokens which are also groups, try to expand them recursively
      for group in tokens_group:
         tests |= self.get_tests(group)
      return tests

   def get_groups(self, test):
      '''Returns the groups that the test belongs to'''
      return set(group for group in self.groups.keys() if test in self.get_tests(group))
