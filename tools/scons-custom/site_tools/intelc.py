"""SCons.Tool.icl

Tool-specific initialization for the Intel C/C++ compiler.
Supports Linux and Windows compilers, v7 and up.

There normally shouldn't be any need to import this module directly.
It will usually be imported through the generic SCons.Tool.Tool()
selection method.

"""

#
# Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013 The SCons Foundation
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
# KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
from __future__ import division

__revision__ = "src/engine/SCons/Tool/intelc.py  2013/03/03 09:48:35 garyo"

import math, sys, os.path, glob, string, re, subprocess, shlex
import utils as sa
from utils.version import Version

is_windows = sys.platform == 'win32'
is_win64 = is_windows and (os.environ['PROCESSOR_ARCHITECTURE'] == 'AMD64' or
                           ('PROCESSOR_ARCHITEW6432' in os.environ and
                            os.environ['PROCESSOR_ARCHITEW6432'] == 'AMD64'))
is_linux = sys.platform.startswith('linux')
is_mac     = sys.platform == 'darwin'

if is_windows:
    import SCons.Tool.msvc
elif is_linux:
    import SCons.Tool.gcc
elif is_mac:
    import SCons.Tool.gcc
import SCons.Util
import SCons.Warnings

# Exceptions for this tool
class IntelCError(SCons.Errors.InternalError):
    pass
class MissingRegistryError(IntelCError): # missing registry entry
    pass
class MissingDirError(IntelCError):     # dir not found
    pass
class NoRegistryModuleError(IntelCError): # can't read registry at all
    pass

# In windows this will hold the root entry in the registry for every detected
# intel compiler version. In linux the root directory is stored instead.
detected_versions = {}

# In order to update the following table with Intel Compiler versions
# we can check the official Intel's article:
#    http://software.intel.com/en-us/articles/intel-compiler-and-composer-update-version-numbers-to-compiler-version-number-mapping

if is_windows:
    # In windows we can only query the register for 'Version Major',
    # 'Version Minor' and 'Revision' (aka build number).
    # From version 12.0 (Intel C++ Composer XE 2011) the build number maps
    # to a given patch level (aka Update number)
    known_versions = {
        # (major, minor, patch):[major,minor,update])
        (17,0,210):[17,0,4], ## Composer XE 2017 Update 4
        (17,0,187):[17,0,2], ## Composer XE 2017 Update 2
        (17,0,143):[17,0,1], ## Composer XE 2017 Update 1
        (17,0,109):[17,0,0], ## Composer XE 2017
        (16,0,246):[16,0,4], ## Composer XE 2016 Update 4
        (16,0,207):[16,0,3], #       .
        (16,0,180):[16,0,2], #       .
        (16,0,146):[16,0,1], ## Composer XE 2016 Update 1
        (16,0,110):[16,0,0], ## Composer XE 2016
        (15,0,287):[15,0,7], ## Composer XE 2015 Update 7
        (15,0,285):[15,0,6], #       .
        (15,0,280):[15,0,5], #       .
        (15,0,221):[15,0,4], #       .
        (15,0,208):[15,0,3], #       .
        (15,0,179):[15,0,2], #       .
        (15,0,148):[15,0,1], ## Composer XE 2015 Update 1
        (15,0,108):[15,0,0], ## Composer XE 2015
        (14,0,237):[14,0,4], ## Composer XE 2013 SP1 Update 4
        (14,0,202):[14,0,3], #       .
        (14,0,176):[14,0,2], #       .
        (14,0,139):[14,0,1], ## Composer XE 2013 SP1 Update 1
        (14,0,103):[14,0,0], ## Composer XE 2013 SP1
        (13,1,198):[13,1,3], ## Composer XE 2013 Update 5
        (13,1,190):[13,1,2], #       .
        (13,1,171):[13,1,1], #       .
        (13,1,149):[13,1,0], #       .
        (13,0,119):[13,0,1], ## Composer XE 2013 Update 1
        (13,0, 89):[13,0,0], ## Composer XE 2013
        (12,1,371):[12,1,7], ## Composer XE 2011 SP1 Update 13
        (12,1,369):[12,1,6], #       .
        (12,1,344):[12,1,5], #       .
        (12,1,325):[12,1,4], #       .
        (12,1,300):[12,1,3], #       .
        (12,1,278):[12,1,2], #       .
        (12,1,258):[12,1,1], #       .
        (12,1,233):[12,1,0], ## Composer XE 2011 SP1 Update 6
        (12,0,221):[12,0,5], ## Composer XE 2011 Update 5
        (12,0,196):[12,0,4], #       .
        (12,0,175):[12,0,3], #       .
        (12,0,154):[12,0,2], #       .
        (12,0,128):[12,0,1], ## Composer XE 2011 Update 1
        (12,0,104):[12,0,0]  ## Composer XE 2011
    }

if is_linux:
    # In linux we have to directly ask icc/icpc for the definition of
    # '__INTEL_COMPILER_BUILD_DATE', which maps to a given version.
    # This macro was introduced from icc 8.1 and it's guaranteed to be
    # an increasing number with every update. We can't
    # only rely on '__INTEL_COMPILER' since it only report the major and minor
    # version and there's no way to discover the patch level.
    # We can dump those macros by executing
    #    $ icpc -dM -E -x c++ /dev/null
    known_versions = {
        # __INTEL_COMPILER_BUILD_DATE:[major,minor,[update,]patch]
        '20170428':[17,0,4,196], ## Composer XE 2017 Update 4
        '20170404':[17,0,3,191], #       .
        '20170213':[17,0,2,174], #       .
        '20161005':[17,0,1,132], ## Composer XE 2017 Update 1
        '20160721':[17,0,0, 98], ## Composer XE 2017
        '20160811':[16,0,4,258], ## Composer XE 2016 Update 4
        '20160415':[16,0,3,210], #       .
        '20160204':[16,0,2,181], #       .
        '20151021':[16,0,1,150], ## Composer XE 2016 Update 1
        '20150816':[16,0,0,109], ## Composer XE 2016
        '20160518':[15,0,7,235], ## Composer XE 2015 Update 7
        '20151119':[15,0,6,233], #       .
        '20150805':[15,0,5,232], ## Composer XE 2015 Update 5
        '20150407':[15,0,3,187], ## Composer XE 2015 Update 3
        '20150121':[15,0,2,164], #       .
        '20141023':[15,0,1,133], ## Composer XE 2015 Update 1
        '20140723':[15,0,0, 90], ## Composer XE 2015
        '20140805':[14,0,4,211], ## Composer XE 2013 SP1 Update 4
        '20140422':[14,0,3,174], #       .
        '20140120':[14,0,2,144], #       .
        '20131008':[14,0,1,106], ## Composer XE 2013 SP1 Update 1
        '20130728':[14,0,0, 80], ## Composer XE 2013 SP1
        '20130607':[13,1,3,192], ## Composer XE 2013 Update 5
        '20130514':[13,1,2,183], #       .
        '20130313':[13,1,1,163], #       .
        '20130121':[13,1,0,146], #       .
        '20121010':[13,0,1,117], ## Composer XE 2013 Update 1
        '20120731':[13,0,0, 79], ## Composer XE 2013
        '20120928':[12,1,7,367], ## Composer XE 2011 SP1 Update 13
        '20120821':[12,1,6,361], #       .
        '20120612':[12,1,5,339], #       .
        '20120410':[12,1,4,319], #       .
        '20120212':[12,1,3,293], #       .
        '20111128':[12,1,2,273], #       .
        '20111011':[12,1,1,256], #       .
        '20110811':[12,1,0,233], ## Composer XE 2011 SP1 Update 6
        '20110719':[12,0,5,220], ## Composer XE 2011 Update 5
        '20110427':[12,0,4,191], #       .
        '20110309':[12,0,3,174], #       .
        '20110117':[12,0,2,137], #       .
        '20101116':[12,0,1,108], ## Composer XE 2011 Update 1
        '20101006':[12,0,0, 84], ## Composer XE 2011
        '20110708':[11,1,   80], ## Compiler Pro 11.1 Update 9
        '20101201':[11,1,   75], #       .
        '20100806':[11,1,   73], #       .
        '20100414':[11,1,   72], #       .
        '20100203':[11,1,   69], #       .
        '20091130':[11,1,   64], #       .
        '20091012':[11,1,   59], #       .
        '20090827':[11,1,   56], #       .
        '20090630':[11,1,   46], ## Compiler Pro 11.1 Update 1
        '20090511':[11,1,   38], ## Compiler Pro 11.1
        '20090609':[11,0,   84]
    }

if is_mac:
    # The version in OSX is defined the same way as in linux, through the
    # predefined macro '__INTEL_COMPILER_BUILD_DATE', which maps to a given version.
    # FIXME: intel built versions 16.0.0 and 15.0.5 the same day, so this
    #        translates in a duplicated entry in the dictionary (key '20150805').
    #        In any case, it's not a big deal, since we are not using icc in OSX.
    known_versions = {
        # __INTEL_COMPILER_BUILD_DATE:[major,minor,[update,]patch]
        '20170430':[17,0,4,181], ## Composer XE 2017 Update 4
        '20170213':[17,0,2,163], ## Composer XE 2017 Update 2
        '20161013':[17,0,1,126], ## Composer XE 2017 Update 1
        '20160721':[17,0,0,102], ## Composer XE 2017
        '20160811':[16,0,4,210], ## Composer XE 2016 Update 4
        '20160416':[16,0,3,170], #
        '20160204':[16,0,2,146], #       .
        '20151020':[16,0,1,111], ## Composer XE 2016 Update 1
        '20150805':[16,0,0, 83], ## Composer XE 2016
        '20160518':[15,0,7,234], ## Composer XE 2015 Update 7
        '20151124':[15,0,6,232], #       .
        '20150805':[15,0,5,222], ## Composer XE 2015 Update 5
        '20150407':[15,0,3,187], ## Composer XE 2015 Update 3
        '20150121':[15,0,2,132], #       .
        '20141022':[15,0,1,108], ## Composer XE 2015 Update 1
        '20140716':[15,0,0, 77], ## Composer XE 2015
        '20140815':[14,0,4,201], ## Composer XE 2013 SP1 Update 4
        '20140421':[14,0,3,166], #       .
        '20140121':[14,0,2,139], #       .
        '20131010':[14,0,1,103], ## Composer XE 2013 SP1 Update 1
        '20130710':[14,0,0, 65], ## Composer XE 2013 SP1
        '20130606':[13,0,3,198], ## Composer XE 2013 Update 5
        '20130314':[13,0,2,171], ## Composer XE 2013 Update 3
        '20121010':[13,0,1,119], ## Composer XE 2013 Update 1
        '20120731':[13,0,0, 88]  ## Composer XE 2013
        }

def linux_ver_normalize(vstr):
    """Normalize a Linux compiler version number.
    Intel changed from "80" to "9.0" in 2005, so we assume if the number
    is greater than 60 it's an old-style number and otherwise new-style.
    Always returns an old-style float like 80 or 90 for compatibility with Windows.
    Shades of Y2K!"""
    # Check for version number like 9.1.026: return 91.026
    # XXX needs to be updated for 2011+ versions (like 2011.11.344 which is compiler v12.1.5)
    m = re.match(r'([0-9]+)\.([0-9]+)\.([0-9]+)', vstr)
    if m:
        vmaj,vmin,build = m.groups()
        return float(vmaj) * 10. + float(vmin) + float(build) / 1000.;
    else:
        f = float(vstr)
        if is_windows:
            return f
        else:
            if f < 60: return f * 10.0
            else: return f

def check_abi(abi):
    """Check for valid ABI (application binary interface) name,
    and map into canonical one"""
    if not abi:
        return None
    abi = abi.lower()
    # valid_abis maps input name to canonical name
    if is_windows:
        valid_abis = {'ia32'  : 'ia32',
                      'x86'   : 'ia32',
                      'ia64'  : 'ia64',
                      'em64t' : 'em64t',
                      'amd64' : 'em64t'}
    if is_linux:
        valid_abis = {'ia32'   : 'ia32',
                      'x86'    : 'ia32',
                      'x86_64' : 'x86_64',
                      'em64t'  : 'x86_64',
                      'amd64'  : 'x86_64'}
    if is_mac:
        valid_abis = {'ia32'   : 'ia32',
                      'x86'    : 'ia32',
                      'x86_64' : 'x86_64',
                      'em64t'  : 'x86_64'}
    try:
        abi = valid_abis[abi]
    except KeyError:
        raise SCons.Errors.UserError("Intel compiler: Invalid ABI %s, valid values are %s"% \
              (abi, list(valid_abis.keys())))
    return abi

def vercmp(a, b):
    """Compare strings as floats,
    but Intel changed Linux naming convention at 9.0"""
    return cmp(linux_ver_normalize(b), linux_ver_normalize(a))

def split_version(v):
    """Splits a version string 'a.b.c' into an int list [a, b, c]"""
    try:
        s = [int(x) for x in v.split('.')]
    except ValueError:
        return []
    return s

def get_version_from_list(v, vlist):
    """See if we can match v (string) in vlist (list of strings)
    If user requests v=13, it will return the greatest 13.x version.
    If user requests v=13.1, it will return the greatest 13.1.x version, and so on"""
    s = split_version(v)
    l = len(s)
    # Try to match the latest installed version of the requested version
    if l > 0:
        for vi in vlist:
            S = split_version(vi)
            if l > len(S):
                continue
            n = 0
            for i in range(l):
                if s[i] == S[i]: n = n + 1
                else:            break
            if n == l:
                return vi
    return None

def detect_installed_versions_linux():
    versions = []
    global detected_versions
    for root, dirs, names in os.walk('/opt'):
        if ('icpc' in names) and not os.path.islink(os.path.join(root, 'icpc')):
            cmd = str(os.path.join(root, 'icpc')) + ' -dM -E -x c++ /dev/null'
            r, lines = sa.system.execute(cmd)
            ver = None
            for l in lines:
                if l.find('__INTEL_COMPILER ') > 0:
                    v  = int(l.split()[2])
                    ver = '%u.%u' % ( int(v/100), int((v%100)/10) )
                if l.find('__INTEL_COMPILER_BUILD_DATE ') > 0:
                    v   = known_versions.get(l.split()[2])
                    if v: ver = '%u.%u.%u' % (v[0], v[1], v[2])
                    break
            if ver:
                detected_versions[ver] = os.path.abspath(os.path.join(root, os.path.pardir, os.path.pardir))
                versions.append(ver)
    return versions

def detect_installed_versions_windows():
   def open_key(name):
      try:
         return SCons.Util.RegOpenKeyEx(SCons.Util.HKEY_LOCAL_MACHINE, name)
      except WindowsError:
         None

   def list_subkeys(key):
      subkeys = []
      while True:
         try: subkey = SCons.Util.RegEnumKey(key, len(subkeys))
         except EnvironmentError: break
         subkeys.append(subkey)
      if 'Defaults' in subkeys: subkeys.remove('Defaults')
      return subkeys

   def get_value(key, name):
      try:
         values = []
         for n in name:
            try: v = SCons.Util.RegQueryValueEx(key, n)[0]
            except WindowsError: v = None
            values.append(v)
         return tuple(values)
      except TypeError:
         try: v = SCons.Util.RegQueryValueEx(key, name)[0]
         except WindowsError: v = None
         return v

   keys = []
   root = 'Software\\Wow6432Node\\Intel\\' if is_win64 else 'Software\\Intel\\'
   for base in ['Compilers\\C++', 'Suites']:
      name = '{}\\{}'.format(root, base)
      key = open_key(name)
      if not key:
         continue
      for version in list_subkeys(key):
         name = '{}\\{}\\{}'.format(root, base, version)
         key = open_key(name)
         if not key:
            continue
         if base == 'Suites':
            for uuid in list_subkeys(key):
               name = '{}\\{}\\{}\\{}\\C++'.format(root, base, version, uuid)
               key = open_key(name)
               if not key:
                  continue
               keys.append(name)
         else:
            keys.append(name)

   versions = []
   global detected_versions

   for k in keys:
      key = open_key(k)
      if not key:
         continue
      ver_nms = ['Major Version', 'Minor Version', 'Revision']
      ver_reg = get_value(key, ver_nms)
      if any((x is None for x in ver_reg)):
         continue
      v = known_versions.get(ver_reg)
      if v: ver = '%u.%u.%u'   % (v[0], v[1], v[2])
      else: ver = '%u.%u.%03u' % ver_reg
      versions.append(ver)
      detected_versions[ver] = k

   return versions

def get_intel_registry_value(valuename, version=None, abi=None):
    """
    Return a value from the Intel compiler registry tree. (Windows only)
    """
    # Open the key:
    if is_win64:
        ABI = '\\EM64T_NATIVE'
    else:
        ABI = '\\'+abi.upper()
    try:
        K  = detected_versions[version] + ABI
        k1 = SCons.Util.RegOpenKeyEx(SCons.Util.HKEY_LOCAL_MACHINE, detected_versions[version] + ABI)
        k2 = SCons.Util.RegOpenKeyEx(SCons.Util.HKEY_LOCAL_MACHINE, detected_versions[version])
    except SCons.Util.RegError:
        raise MissingRegistryError("%s was not found in the registry, for Intel compiler version %s, abi='%s'"%(K, version, abi))

    # Get the value:
    # First try in key 'k1' and then in 'k2'
    try:
        v = SCons.Util.RegQueryValueEx(k1, valuename)[0]
        return v  # or v.encode('iso-8859-1', 'replace') to remove unicode?
    except SCons.Util.RegError:
        try:
            v = SCons.Util.RegQueryValueEx(k2, valuename)[0]
            return v  # or v.encode('iso-8859-1', 'replace') to remove unicode?
        except SCons.Util.RegError:
            raise MissingRegistryError("%s\\%s was not found in the registry."%(K, valuename))

def detect_installed_versions():
    """Returns a sorted list of strings, like "70" or "80" or "9.0"
    with most recent compiler version first.
    """
    versions=[]
    global detected_versions
    if is_windows:
        versions = detect_installed_versions_windows()
        if not versions or len(versions) == 0:
            if is_win64:
                keyname = 'Software\\WoW6432Node\\Intel\\Compilers\\C++'
            else:
                keyname = 'Software\\Intel\\Compilers\\C++'
            try:
                k = SCons.Util.RegOpenKeyEx(SCons.Util.HKEY_LOCAL_MACHINE,
                                            keyname)
            except WindowsError:
                return []
            i = 0
            versions = []
            try:
                while i < 100:
                    subkey = SCons.Util.RegEnumKey(k, i) # raises EnvironmentError
                    # Check that this refers to an existing dir.
                    # This is not 100% perfect but should catch common
                    # installation issues like when the compiler was installed
                    # and then the install directory deleted or moved (rather
                    # than uninstalling properly), so the registry values
                    # are still there.
                    ok = False
                    for try_abi in ('IA32', 'IA32e',  'IA64', 'EM64T'):
                        try:
                            d = get_intel_registry_value('ProductDir', subkey, try_abi)
                        except MissingRegistryError:
                            continue  # not found in reg, keep going
                        if os.path.exists(d): ok = True
                    if ok:
                        versions.append(subkey)
                    else:
                        try:
                            # Registry points to nonexistent dir.  Ignore this
                            # version.
                            value = get_intel_registry_value('ProductDir', subkey, 'IA32')
                        except MissingRegistryError, e:

                            # Registry key is left dangling (potentially
                            # after uninstalling).

                            print \
                                "scons: *** Ignoring the registry key for the Intel compiler version %s.\n" \
                                "scons: *** It seems that the compiler was uninstalled and that the registry\n" \
                                "scons: *** was not cleaned up properly.\n" % subkey
                        else:
                            print "scons: *** Ignoring "+str(value)

                    i = i + 1
            except EnvironmentError:
                # no more subkeys
                pass
    elif is_linux or is_mac:
        versions = detect_installed_versions_linux()
        if not versions or len(versions) == 0:
            for d in glob.glob('/opt/intel_cc_*'):
                # Typical dir here is /opt/intel_cc_80.
                m = re.search(r'cc_(.*)$', d)
                if m:
                    versions.append(m.group(1))
            for d in glob.glob('/opt/intel/cc*/*'):
                # Typical dir here is /opt/intel/cc/9.0 for IA32,
                # /opt/intel/cce/9.0 for EMT64 (AMD64)
                m = re.search(r'([0-9][0-9.]*)$', d)
                if m:
                    versions.append(m.group(1))
            for d in glob.glob('/opt/intel/Compiler/*'):
                # Typical dir here is /opt/intel/Compiler/11.1
                m = re.search(r'([0-9][0-9.]*)$', d)
                if m:
                    versions.append(m.group(1))
            for d in glob.glob('/opt/intel/composerxe-*'):
                # Typical dir here is /opt/intel/composerxe-2011.4.184
                m = re.search(r'([0-9][0-9.]*)$', d)
                if m:
                    versions.append(m.group(1))
            for d in glob.glob('/opt/intel/composer_xe_*'):
                # Typical dir here is /opt/intel/composer_xe_2011_sp1.11.344
                # The _sp1 is useless, the installers are named 2011.9.x, 2011.10.x, 2011.11.x
                m = re.search(r'([0-9]{0,4})(?:_sp\d*)?\.([0-9][0-9.]*)$', d)
                if m:
                    versions.append("%s.%s"%(m.group(1), m.group(2)))
    def keyfunc(str):
        """Given a dot-separated version string, return a tuple of ints representing it."""
        return [int(x) for x in str.split('.')]
    # split into ints, sort, then remove dups
    return sorted(SCons.Util.unique(versions), key=keyfunc, reverse=True)

def get_intel_compiler_top(version, abi):
    """
    Return the main path to the top-level dir of the Intel compiler,
    using the given version.
    The compiler will be in <top>/bin/icl.exe (icc on linux),
    the include dir is <top>/include, etc.
    """

    if is_windows:
        if not SCons.Util.can_read_reg:
            raise NoRegistryModuleError("No Windows registry module was found")
        top = get_intel_registry_value('ProductDir', version, abi)
        archdir={'x86_64': 'intel64',
                 'amd64' : 'intel64',
                 'em64t' : 'intel64',
                 'x86'   : 'ia32',
                 'i386'  : 'ia32',
                 'ia32'  : 'ia32'
        }[abi] # for v11 and greater
        # pre-11, icl was in Bin.  11 and later, it's in Bin/<abi> apparently.
        if not os.path.exists(os.path.join(top, "Bin", "icl.exe")) \
              and not os.path.exists(os.path.join(top, "Bin", abi, "icl.exe")) \
              and not os.path.exists(os.path.join(top, "Bin", archdir, "icl.exe")):
            raise MissingDirError("Can't find Intel compiler in %s"%(top))
    elif is_mac or is_linux:
        def find_in_2008style_dir(version):
            # first dir is new (>=9.0) style, second is old (8.0) style.
            dirs=('/opt/intel/cc/%s', '/opt/intel_cc_%s')
            if abi == 'x86_64':
                dirs=('/opt/intel/cce/%s',)  # 'e' stands for 'em64t', aka x86_64 aka amd64
            top=None
            for d in dirs:
                if os.path.exists(os.path.join(d%version, "bin", "icc")):
                    top = d%version
                    break
            return top
        def find_in_2010style_dir(version):
            dirs=('/opt/intel/Compiler/%s/*'%version)
            # typically /opt/intel/Compiler/11.1/064 (then bin/intel64/icc)
            dirs=glob.glob(dirs)
            # find highest sub-version number by reverse sorting and picking first existing one.
            dirs.sort()
            dirs.reverse()
            top=None
            for d in dirs:
                if (os.path.exists(os.path.join(d, "bin", "ia32", "icc")) or
                    os.path.exists(os.path.join(d, "bin", "intel64", "icc"))):
                    top = d
                    break
            return top
        def find_in_2011style_dir(version):
            # The 2011 (compiler v12) dirs are inconsistent, so just redo the search from
            # get_all_compiler_versions and look for a match (search the newest form first)
            top=None
            for d in glob.glob('/opt/intel/composer_xe_*'):
                # Typical dir here is /opt/intel/composer_xe_2011_sp1.11.344
                # The _sp1 is useless, the installers are named 2011.9.x, 2011.10.x, 2011.11.x
                m = re.search(r'([0-9]{0,4})(?:_sp\d*)?\.([0-9][0-9.]*)$', d)
                if m:
                    cur_ver = "%s.%s"%(m.group(1), m.group(2))
                    if cur_ver == version and \
                        (os.path.exists(os.path.join(d, "bin", "ia32", "icc")) or
                        os.path.exists(os.path.join(d, "bin", "intel64", "icc"))):
                        top = d
                        break
            if not top:
                for d in glob.glob('/opt/intel/composerxe-*'):
                    # Typical dir here is /opt/intel/composerxe-2011.4.184
                    m = re.search(r'([0-9][0-9.]*)$', d)
                    if m and m.group(1) == version and \
                        (os.path.exists(os.path.join(d, "bin", "ia32", "icc")) or
                        os.path.exists(os.path.join(d, "bin", "intel64", "icc"))):
                            top = d
                            break
            return top
        def find_in_2016style_dir(version):
            # The 2016 (compiler v16) dirs are inconsistent from previous.
            top = None
            for d in glob.glob('/opt/intel/compilers_and_libraries_%s/linux'%version):
                if os.path.exists(os.path.join(d, "bin", "ia32", "icc")) or os.path.exists(os.path.join(d, "bin", "intel64", "icc")):
                    top = d
                    break
            return top

        top = find_in_2016style_dir(version) or find_in_2011style_dir(version) or find_in_2010style_dir(version) or find_in_2008style_dir(version)
        # print "INTELC: top=",top
        top = detected_versions[version]
        if not top:
            raise MissingDirError("Can't find version %s Intel compiler in %s (abi='%s')"%(version,top, abi))
    return top


def generate(env, version=None, abi=None, topdir=None, verbose=0):
    """Add Builders and construction variables for Intel C/C++ compiler
    to an Environment.
    args:
      version: (string) compiler version to use, like "80"
      abi:     (string) 'win32' or whatever Itanium version wants
      topdir:  (string) compiler top dir, like
                         "c:\Program Files\Intel\Compiler70"
                        If topdir is used, version and abi are ignored.
      verbose: (int)    if >0, prints compiler version used.
    """
    if not (is_mac or is_linux or is_windows):
        # can't handle this platform
        return

    if is_windows:
        SCons.Tool.msvc.generate(env)
    elif is_linux:
        SCons.Tool.gcc.generate(env)
    elif is_mac:
        SCons.Tool.gcc.generate(env)

    # if version is unspecified, use latest
    vlist = detect_installed_versions()
    if not version:
        if vlist:
            version = vlist[0]
    else:
        # User may have specified '90' but we need to get actual dirname '9.0'.
        # get_version_from_list does that mapping.
        v = get_version_from_list(version, vlist)
        if not v:
            raise SCons.Errors.UserError("Invalid Intel compiler version %s: "%version + \
                  "installed versions are %s"%(', '.join(vlist)))
        version = v

    env['COMPILER_VERSION_DETECTED']  = Version(version)
    env['COMPILER_VERSION_INSTALLED'] = vlist

    # if abi is unspecified, use ia32
    # alternatives are ia64 for Itanium, or amd64 or em64t or x86_64 (all synonyms here)
    abi = check_abi(abi)
    if abi is None:
        if is_mac or is_linux:
            # Check if we are on 64-bit linux, default to 64 then.
            uname_m = os.uname()[4]
            if uname_m == 'x86_64':
                abi = 'x86_64'
            else:
                abi = 'ia32'
        else:
            if is_win64:
                abi = 'em64t'
            else:
                abi = 'ia32'

    if version and not topdir:
        try:
            topdir = get_intel_compiler_top(version, abi)
        except (SCons.Util.RegError, IntelCError):
            topdir = None

    if not topdir:
        # Normally this is an error, but it might not be if the compiler is
        # on $PATH and the user is importing their env.
        class ICLTopDirWarning(SCons.Warnings.Warning):
            pass
        if (is_mac or is_linux) and not env.Detect('icc') or \
           is_windows and not env.Detect('icl'):

            SCons.Warnings.enableWarningClass(ICLTopDirWarning)
            SCons.Warnings.warn(ICLTopDirWarning,
                                "Failed to find Intel compiler for version='%s', abi='%s'"%
                                (str(version), str(abi)))
        else:
            # should be cleaned up to say what this other version is
            # since in this case we have some other Intel compiler installed
            SCons.Warnings.enableWarningClass(ICLTopDirWarning)
            SCons.Warnings.warn(ICLTopDirWarning,
                                "Can't find Intel compiler top dir for version='%s', abi='%s'"%
                                    (str(version), str(abi)))

    if topdir:
        archdir={'x86_64': 'intel64',
                 'amd64' : 'intel64',
                 'em64t' : 'intel64',
                 'x86'   : 'ia32',
                 'i386'  : 'ia32',
                 'ia32'  : 'ia32'
        }[abi] # for v11 and greater
        if os.path.exists(os.path.join(topdir, 'bin', archdir)):
            bindir="bin/%s"%archdir
            libdir="lib/%s"%archdir
        else:
            bindir="bin"
            libdir="lib"
        if verbose:
            print "Intel C compiler: using version %s (%g), abi %s, in '%s/%s'"%\
                  (repr(version), linux_ver_normalize(version),abi,topdir,bindir)
            if is_linux:
                # Show the actual compiler version by running the compiler.
                os.system('%s/%s/icc --version'%(topdir,bindir))
            if is_mac:
                # Show the actual compiler version by running the compiler.
                os.system('%s/%s/icc --version'%(topdir,bindir))

        env['INTEL_C_COMPILER_TOP'] = topdir
        if is_linux:
            paths={'INCLUDE'         : 'include',
                   'LIB'             : libdir,
                   'PATH'            : bindir,
                   'LD_LIBRARY_PATH' : libdir}
            for p in paths.keys():
                env.PrependENVPath(p, os.path.join(topdir, paths[p]))
        if is_mac:
            paths={'INCLUDE'         : 'include',
                   'LIB'             : libdir,
                   'PATH'            : bindir,
                   'LD_LIBRARY_PATH' : libdir}
            for p in paths.keys():
                env.PrependENVPath(p, os.path.join(topdir, paths[p]))
        if is_windows:
            #       env key    reg valname   default subdir of top
            paths=(('INCLUDE', 'IncludeDir', 'Include'),
                   ('LIB'    , 'LibDir',     'Lib'),
                   ('PATH'   , 'BinDir',     bindir))
            # We are supposed to ignore version if topdir is set, so set
            # it to the emptry string if it's not already set.
            if version is None:
                version = ''
            # Each path has a registry entry, use that or default to subdir
            for p in paths:
                try:
                    path=get_intel_registry_value(p[1], version, abi)
                    # These paths may have $(ICInstallDir)
                    # which needs to be substituted with the topdir.
                    path=path.replace('$(ICInstallDir)', topdir + os.sep)
                except IntelCError:
                    # Couldn't get it from registry: use default subdir of topdir
                    env.PrependENVPath(p[0], os.path.join(topdir, p[2]))
                else:
                    env.PrependENVPath(p[0], path.split(os.pathsep))
                    # print "ICL %s: %s, final=%s"%(p[0], path, str(env['ENV'][p[0]]))

    if is_windows:
        env['CC']        = 'icl'
        env['CXX']       = 'icl'
        env['LINK']      = 'xilink'
        env['AR']        = 'xilib'
    else:
        env['CC']        = 'icc'
        env['CXX']       = 'icpc'
        # Don't reset LINK here;
        # use smart_link which should already be here from link.py.
        #env['LINK']      = '$CC'
        env['AR']        = 'xiar'
        env['LD']        = 'xild' # not used by default

    # This is not the exact (detailed) compiler version,
    # just the major version as determined above or specified
    # by the user.  It is a float like 80 or 90, in normalized form for Linux
    # (i.e. even for Linux 9.0 compiler, still returns 90 rather than 9.0)
    if version:
        env['INTEL_C_COMPILER_VERSION']=linux_ver_normalize(version)

    if is_windows:
        # Look for license file dir
        # in system environment, registry, and default location.
        envlicdir = os.environ.get("INTEL_LICENSE_FILE", '')
        K = ('SOFTWARE\Intel\Licenses')
        try:
            k = SCons.Util.RegOpenKeyEx(SCons.Util.HKEY_LOCAL_MACHINE, K)
            reglicdir = SCons.Util.RegQueryValueEx(k, "w_cpp")[0]
        except (AttributeError, SCons.Util.RegError):
            reglicdir = ""
        defaultlicdir = r'C:\Program Files\Common Files\Intel\Licenses'

        licdir = None
        for ld in [envlicdir, reglicdir]:
            # If the string contains an '@', then assume it's a network
            # license (port@system) and good by definition.
            if ld and (ld.find('@') != -1 or os.path.exists(ld)):
                licdir = ld
                break
        if not licdir:
            licdir = defaultlicdir
            if not os.path.exists(licdir):
                class ICLLicenseDirWarning(SCons.Warnings.Warning):
                    pass
                SCons.Warnings.enableWarningClass(ICLLicenseDirWarning)
                SCons.Warnings.warn(ICLLicenseDirWarning,
                                    "Intel license dir was not found."
                                    "  Tried using the INTEL_LICENSE_FILE environment variable (%s), the registry (%s) and the default path (%s)."
                                    "  Using the default path as a last resort."
                                        % (envlicdir, reglicdir, defaultlicdir))
        env['ENV']['INTEL_LICENSE_FILE'] = licdir

    # In our CentOS 6 machines, we are using the custom gcc 4.8.2 toolchain from
    # http://people.centos.org/tru/devtools-2/readme. We should point to it in
    # order to use C++11 features.
    if sa.system.is_linux:
      gcc_toolchain = '/opt/rh/devtoolset-2/root/usr'
      if os.path.exists(gcc_toolchain):
         env['GCC_SUPPORT_ROOT'] = gcc_toolchain
         env.PrependENVPath('PATH', os.path.join(gcc_toolchain, 'bin'))

def exists(env):
    if not (is_mac or is_linux or is_windows):
        # can't handle this platform
        return 0

    try:
        versions = detect_installed_versions()
    except (SCons.Util.RegError, IntelCError):
        versions = None
    detected = versions is not None and len(versions) > 0
    if not detected:
        # try env.Detect, maybe that will work
        if is_windows:
            return env.Detect('icl')
        elif is_linux:
            return env.Detect('icc')
        elif is_mac:
            return env.Detect('icc')
    return detected

# end of file

# Local Variables:
# tab-width:4
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=4 shiftwidth=4:
