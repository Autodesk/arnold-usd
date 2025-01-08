# vim: filetype=python

import os
import re
import hashlib
try:
   # Python 2
   from itertools import izip as zip
except ImportError:
   # Python 3
   pass

def arnold_version(path):
   '''
   Obtains Arnold library version by parsing 'ai_version.h'
   '''
   regex = re.compile(r'^#define[ \t]+AI_VERSION_(?P<name>[^ \t]+)[ \t]+(?P<value>.+)$')
   defines = ['ARCH_NUM', 'MAJOR_NUM', 'MINOR_NUM', 'FIX']
   version = [None] * (len(defines) + 1)
   with open(path, 'r') as f:
      for match in (regex.match(line.strip()) for line in f.readlines()):
         if not match:
            continue
         name = match.group('name')
         if name in defines:
            index = defines.index(name)
            value = match.group('value')
            if name == 'FIX':
               value, _, version[index + 1] = value.strip('"').partition('.')
            version[index] = int(value)
         if all(x is not None for x in version):
            break
   return version

def arnold_package_name(package):
   '''
   Parses a package name in order to extract the version, platform, mode and extension
   '''
   patterns = {
      'version' : r'(?P<version>\d+\.\d+.\d+.\d+(\..+)?)',
      'platform': r'(?P<platform>linux|windows|darwin)',
      'mode'    : r'(?P<mode>profile|dev|debug)', # opt if nothing specified
      'ext'     : r'(?P<extension>.+)',
   }
   pack_re = re.compile(r'^Arnold-{version}-{platform}(-{mode})?\.{ext}$'.format(**patterns))
   m = pack_re.match(os.path.basename(package))
   if not m:
      return {}
   data = m.groupdict()
   if data['mode'] is None:
      data['mode'] = 'opt'
   return data

def file_hash(path, algorithm='sha1'):
   '''
   Calculates the "algorithm" digest of the file
   '''
   algorithm = algorithm.lower()
   if algorithm not in hashlib.algorithms_guaranteed:
      raise
   hasher = getattr(hashlib, algorithm, None)
   if not hasher or not callable(hasher):
      raise
   hasher = hasher()
   with open(path, 'rb') as f:
      block_size = 65536
      while True:
         buffer = f.read(block_size)
         if len(buffer) == 0:
            break
         hasher.update(buffer)
   return hasher.hexdigest()

def permissions(path):
   '''
   Generates the permissions "rwxrwxrwx" string for "path"
   '''
   import grp
   import itertools
   import pwd
   import stat
   all_permissions = [
      stat.S_IRUSR, stat.S_IWUSR, stat.S_IXUSR,
      stat.S_IRGRP, stat.S_IWGRP, stat.S_IXGRP,
      stat.S_IROTH, stat.S_IWOTH, stat.S_IXOTH
   ]
   chars = ['r', 'w', 'x']
   permissions_string = ''
   stat_info = os.stat(path)
   for p, c in zip(all_permissions, itertools.cycle(chars)):
      permissions_string += c if bool(stat_info.st_mode & p) else '-'

   uid = stat_info.st_uid
   gid = stat_info.st_gid

   permissions_string += ' ' + pwd.getpwuid(uid)[0]
   permissions_string += ' ' + grp.getgrgid(gid)[0]

   return permissions_string

def r_getattr(obj, name, *args):
   '''
   Substitute for the builtin getattr(), which also supports nested attributes
   '''
   import functools
   _getattr = getattr if len(args) == 0 else lambda x, y: getattr(x, y, args[0])
   return functools.reduce(_getattr, [obj] + name.split('.'))

try:
   # Python 2
   # Empirically, it is faster to check explicitly for str and
   # unicode than for basestring.
   string_types = (str, unicode)
except NameError:
   # Python 3
   string_types = (str)

def is_string(obj):
   return isinstance(obj, string_types)

def msvc_version_tuple(string):
   # For MSVC, the usual specification of the version only includes the first digit
   # of the second number (eg. 14.3 for both 14.34.2334 and also 14.38.12345).
   # Let's properly split the version so we can easily sort and select
   # (eg. 14.3.4.2334 for 14.34.2334)
   str_tokens = string.split('.')
   tokens = str_tokens[:1]
   if len(str_tokens) > 1:
      if len(str_tokens[1]) > 1:
            tokens += [str_tokens[1][:1], str_tokens[1][1:]]
            if len(str_tokens) > 2:
               tokens += str_tokens[2:]
      else:
            tokens += [str_tokens[1]]
   return tuple(int(_) for _ in tokens)

def static_vars(**kwargs):
   # Decorator for mimic'ing static vars in functions
   def decorate(func):
      for k in kwargs:
         setattr(func, k, kwargs[k])
      return func
   return decorate
