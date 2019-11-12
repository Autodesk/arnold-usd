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
from .contrib.elftools.common.exceptions import ELFError
from .contrib.elftools.common.py3compat import bytes2str
from .contrib.elftools.elf.elffile import ELFFile
from .contrib.elftools.elf.dynamic import DynamicSection
from .contrib.elftools.elf.gnuversions import GNUVerSymSection, GNUVerDefSection, GNUVerNeedSection
from .version import Version

def _get_symbol_version_info(versioninfo, nsym):
    '''
    Return a dict containing information on the symbol version
    or None if no version information is available
    '''
    symbol_version = dict.fromkeys(('index', 'name', 'filename', 'hidden'))

    if not versioninfo['versym'] or nsym >= versioninfo['versym'].num_symbols():
        return None

    symbol = versioninfo['versym'].get_symbol(nsym)
    index = symbol.entry['ndx']
    if not index in ['VER_NDX_LOCAL', 'VER_NDX_GLOBAL']:
        index = int(index)

        if versioninfo['type'] == 'GNU':
            # In GNU versioning mode, the highest bit is used to
            # store wether the symbol is hidden or not
            if index & 0x8000:
                index &= ~0x8000
                symbol_version['hidden'] = True

        if versioninfo['verdef'] and index <= versioninfo['verdef'].num_versions():
            _, verdaux_iter = versioninfo['verdef'].get_version(index)
            symbol_version['name'] = bytes2str(next(verdaux_iter).name)
        else:
            verneed, vernaux = versioninfo['verneed'].get_version(index)
            symbol_version['name'] = bytes2str(vernaux.name)
            symbol_version['filename'] = bytes2str(verneed.name)

    symbol_version['index'] = index
    return symbol_version


def get_maximum_symbol_version(filename):
   '''
   Return a dict containing information about the maximum versioned symbols in the library
   '''
   with open(filename, 'rb') as file:
      sv = {}
      try:
         versioninfo = {'versym': None, 'verdef': None, 'verneed': None, 'type': None}
         elf_file = ELFFile(file)
         for section in elf_file.iter_sections():
            if   isinstance(section, GNUVerSymSection ): versioninfo['versym']  = section
            elif isinstance(section, GNUVerDefSection ): versioninfo['verdef']  = section
            elif isinstance(section, GNUVerNeedSection): versioninfo['verneed'] = section
            elif isinstance(section, DynamicSection   ):
               for tag in section.iter_tags():
                  if tag['d_tag'] == 'DT_VERSYM':
                     versioninfo['type'] = 'GNU'
                     break

         if not versioninfo['type'] and (versioninfo['verneed'] or versioninfo['verdef']):
            versioninfo['type'] = 'Solaris'

         if not versioninfo['type'] or not versioninfo['versym']:
            return sv

         for idx in xrange(versioninfo['versym'].num_symbols()):
            symbol_version = _get_symbol_version_info(versioninfo, idx)
            if symbol_version['index'] not in ['VER_NDX_LOCAL', 'VER_NDX_GLOBAL']:
               version = symbol_version['name'].partition('_')
               if version[1] == '_' and version[2]:
                  prefix = version[0]
                  version = Version(version[2])
                  if version > sv.get(prefix, Version(None)):
                     sv[prefix] = version
         return sv
      except ELFError as ex:
         return sv
