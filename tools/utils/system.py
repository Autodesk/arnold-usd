# vim: filetype=python

import platform
import collections
try:
   import psutil
except ImportError:
   psutil = None
import shlex
try:
   import subprocess32 as subprocess
except ImportError:
   import subprocess
import signal
import sys
import threading
import time
from os import environ as osenviron
from os import path    as ospath
from os import pathsep as ospathsep

from .     import devtoolset
from .misc import is_string
from .misc import static_vars

try:
   # The only option after Python 3.11
   from inspect import getfullargspec as inspect_getargspec
   popen_argspec = inspect_getargspec(subprocess.Popen.__init__)
   popen_argspec = popen_argspec.args + popen_argspec.kwonlyargs
except ImportError:
   # Deprecated in 3.0 and removed in Python 3.11
   from inspect import getargspec as inspect_getargspec
   popen_argspec = inspect_getargspec(subprocess.Popen.__init__).args

# Obtain information about the system only once, when loaded
os = platform.system().lower()

_linux   = 'linux'
_darwin  = 'darwin'
_windows = 'windows'

_Allowed = collections.namedtuple('_Allowed', ['os'])

# allowed.os is a list of allowed operative system
allowed = _Allowed(
     os = [_linux, _darwin, _windows]
)

# These data avoid writing error prone checks like "os.system.os == 'linux'"
is_linux   = os == _linux
is_darwin  = os == _darwin
is_windows = os == _windows
is_unix    = not is_windows

LIB_EXTENSION = {
   _linux: '.so',
   _darwin: '.dylib',
   _windows: '.dll',
}.get(os, '')

arch = platform.machine().lower()

_x86_64 = 'x86_64'
_arm64  = 'arm64'

if arch in ('x64', 'amd64', 'intel64', 'em64t'):
    arch = _x86_64
elif arch.startswith('aarch64'):
    arch = _arm64

is_x86_64 = arch == _x86_64
is_arm64  = arch == _arm64

PATH = 'PATH'
LIBRARY_PATH = {
   _linux  : 'LD_LIBRARY_PATH',
   _darwin : 'DYLD_LIBRARY_PATH',
   _windows: 'PATH'
}.get(os, None)

# This "safe" version of "print" works atomically, avoiding the mess caused by
# multiple threads writing at the same time. It has the same declaration and
# behavior as the Python 3.3 print() function:
#   https://docs.python.org/3/library/functions.html?highlight=print#print
def print_safe(*args, **kwargs):
   # Check input parameters
   valid_kwargs = ('sep', 'end', 'file', 'flush')
   for key, value in kwargs.items():
      if key not in valid_kwargs:
         raise TypeError('\'{}\' is an invalid keyword argument for this function'.format(key))
      elif key in ['sep', 'end']:
         not_string = not is_string(value)
         not_None = value is not None
         if not_string and not_None:
            typename = type(value).__name__
            raise TypeError('\'{}\' must be None or a string, not {}'.format(key, typename))
   # Transform objects into strings, get the input parameters, set the default
   # values if not provided and write the whole string. Flush if requested.
   objects = (str(o) for o in args)
   sep = str(kwargs.get('sep', ' '))
   end = str(kwargs.get('end', '\n'))
   out = kwargs.get('file', sys.stdout)
   out.write(sep.join(objects) + end)
   if kwargs.get('flush', False):
      out.flush()

class Killer(threading.Timer):

   devtoolsets = devtoolset.detect()

   def __init__(self, timeout, process, cwd):
      self.timeout = timeout
      self.process = process
      self.cwd     = cwd
      self.killed  = False
      super().__init__(self.timeout, self.kill)

   def kill(self):
      if self.process.returncode is None:
         if is_linux and Killer.devtoolsets:
            # Use gstack for getting the stacktrace of the running process
            # Since we use gstack from the detected devtoolset/gcc-toolset we need
            # to "source" (prepend) the toolset environment so gstack can access to all its
            # needed tools
            gstack_env = dict(osenviron)
            gstack_env['PATH'] = ospathsep.join([Killer.devtoolsets[0][2]] + gstack_env.get('PATH', '').split(ospathsep))
            gstack_cmd = ['gstack', f'{self.process.pid}']
            execute(gstack_cmd, env=gstack_env, logToFile=ospath.join(self.cwd, 'timeout-stack.txt'))
         if is_windows and psutil:
            # Try to create a minidump of the process
            try:
               # Check child itself as well as all subprocesses of the child
               # Compare with the executable to find which process is the relevant one
               relevant_command = self.process.args[0]
               child = psutil.Process(self.process.pid)
               relevant_process = None
               if child.cmdline()[0] == relevant_command:
                  relevant_process = child
               else:
                  children = child.children(recursive=True)
                  for candidate_process in children:
                     if (candidate_process.cmdline()[0] == relevant_command):
                        relevant_process = candidate_process
                        break
               if relevant_process:
                  procdump_command = "procdump -accepteula -mm {}".format(relevant_process.pid)
                  print("Generating minidump for process '{}' using command '{}')".format(" ".join(relevant_process.cmdline()), procdump_command))
                  # Create a minidump of the process
                  retval, err = execute(procdump_command, cwd=self.cwd)
                  print("procdump returned {}:\n{}".format(retval, "\n".join(err)))
               else:
                  print("Could not find child process running {} to generate a minidump for.".format(relevant_command))
            except psutil.Error as e:
               print("Failed to generate minidump: {}".format(e))
               # Ignore.
            # Kill subprocess (recursively)
            subprocess.call(['taskkill', '/F', '/T', '/PID', f'{self.process.pid}'])
            self.killed = True
         elif not is_windows:
            # Send a SIGABRT signal hoping to get a stacktrace from the process
            # TODO: Isn't SIGTERM a more convenient signal for a graceful termination
            # if the process is not hung?
            self.process.send_signal(signal.SIGABRT)
            try:
               # This might deadlock the process. Let's wait some time
               # for more desperate measures
               self.process.wait(20)
               self.killed = True
            except subprocess.TimeoutExpired:
               # The process didn't terminate. Let's send an aggressive
               # SIGKILL signal
               self.process.send_signal(signal.SIGKILL)
               try:
                  # Wait again for SIGKILL to do its job
                  self.process.wait(20)
                  self.killed = True
               except subprocess.TimeoutExpired:
                  # SIGKILL didn't work. Let's inform the user
                  print(f'Failed to kill process {self.process.pid}')

# This mimics https://docs.python.org/3.11/library/subprocess.html#subprocess.CompletedProcess
# so we can return it from our execute() functions
class CompletedProcess:
   def __init__(self, args, returncode, stdout, stderr, timeout):
      self.args       = args
      self.returncode = returncode
      self.stdout     = stdout
      self.stderr     = stderr
      self.timeout    = timeout
      

def execute(cmd, env=None, cwd=None, verbose=False, shell=False, callback=None, timeout=0, logToFile=None, version=1):
   '''
   Executes a command and returns a tuple with the exit code and the output
   '''
   # Things to do before executing the command:
   # - Split cmd into a list if it is a string
   # - Normalize environment to strings
   split_command = is_string(cmd) and not shell
   # Create a dictionary with the arguments for subprocess.Popen()
   redirectOutputToFile = logToFile and not verbose and not callback
   if redirectOutputToFile:
      stdout_f = open(logToFile, 'w')
   popen_args = {
      'args'    : shlex.split(cmd, posix=is_unix) if split_command else cmd,
      'stdout'  : subprocess.PIPE if not redirectOutputToFile else stdout_f,
      'stderr'  : subprocess.STDOUT,
      'cwd'     : cwd,
      'env'     : {k : str(v) for k, v in env.items()} if env else None,
      'shell'   : shell,
      'bufsize' : 1,
   }
   if 'errors' in popen_argspec: popen_args['errors'] = 'replace'
   if 'text'   in popen_argspec: popen_args['text'] = True
   else                        : popen_args['universal_newlines'] = True
   t = time.time()
   try:
      process = subprocess.Popen(**popen_args)
   except OSError as e:
      if version == 1: return e.errno, e.strerror.splitlines()
      else           : return CompletedProcess(popen_args['args'], e.errno, e.strerror.splitlines(), None, False)
   else:
      killer = Killer(timeout, process, cwd)
      if timeout:
         killer.start()
      output = []
      if not redirectOutputToFile:
         for line in iter(process.stdout.readline, b''):
            if not line:
               break
            line = line.rstrip('\n')
            output.append(line)
            if verbose:
               print_safe(line)
            if callback:
               callback(line)
      if process.stdout: process.stdout.close()
      if process.stderr: process.stderr.close()
      process.wait()
      if timeout:
         killer.cancel()
         if killer.is_alive():
            killer.join()
      if redirectOutputToFile:
         stdout_f.flush()
         stdout_f.close()
         # Need to keep the existing function contract
         try:
            with open(logToFile, 'rb') as log:
               output = log.read()
               output = output.decode('utf-8', 'replace')
               output = output.splitlines()
         except IOError as e:
            if version == 1: return e.errno, e.strerror.splitlines()
            else           : return CompletedProcess(popen_args['args'], e.errno, e.strerror.splitlines(), None, killer.killed)
      elif logToFile:
         # We were told to write to file, but we could not because we were also told to log to stdout or callback
         try:
            with open(logToFile, 'w') as log:
               log.write('\n'.join(output))
         except IOError as e:
            if version == 1: return e.errno, e.strerror.splitlines()
            else           : return CompletedProcess(popen_args['args'], e.errno, e.strerror.splitlines(), None, killer.killed)

      if version == 1: return process.returncode, output
      else           : return CompletedProcess(popen_args['args'], process.returncode, output, None, killer.killed)

_info = None

def info(refresh=False):
   ''' Returns a dictionary with all the gathered system information
   '''
   global _info
   if not refresh:
      if _info is not None:
         return _info

   info = {
      'gpu': {},
   }

   # Execute gpu-info to get all the GPU information
   path_root  = ospath.dirname(__file__)
   gpu_info = ospath.join(path_root, 'contrib', 'tools', os, 'gpu-info')
   error, output = execute(gpu_info)
   if not error:
      # gpu-info prints a JSON data structure in the last line. Turn it into
      # a proper python dictionary
      import json
      info['gpu'] = json.loads(output[-1])
      # Let's massage some of the data
      for i in range(len(info['gpu']['devices'])):
         # Convert compute capability string into an int tuple
         cc = info['gpu']['devices'][i].get('compute_capability', '0.0')
         cc = tuple(int(x) for x in cc.split('.'))
         info['gpu']['devices'][i]['compute_capability'] = cc
   else:
      info['gpu']['devices'] = []

   _info = info
   return info
