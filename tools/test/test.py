import os
import re
import sys
import shutil
import glob
import time
from datetime import datetime
import json
try:
   import subprocess32 as subprocess
except ImportError:
   import subprocess

from utils import system
from utils import path

try:
   # Python 2
   # Empirically, it is faster to check explicitly for str and
   # unicode than for basestring.
   string_types = (str, unicode)
except NameError:
   # Python 3
   string_types = (str)

def enum(*sequential, **named):
   enums   = dict(zip(sequential, range(len(sequential))), **named)
   reverse = dict((value, key) for key, value in enums.items())
   enums['name'] = reverse
   enums['enum'] = enums
   return type('Enum', (), enums)

def obfuscate_log(log_file):
   def sig_repl(match):
      obfuscated_sig = '*' * len(match.groupdict().get('sig', ''))
      return str.encode(f'sig="{obfuscated_sig}"')
   if os.path.exists(log_file):
      # We have some test dealing with corrupted binary
      # data which is intentionally printed in the stdout. So the logs
      # contain part of that binary data. So let's open them in binary mode,
      # then decode them replacing the offending data
      with open(log_file, 'rb') as f:
         contents = f.read()
      with open(log_file, 'wb') as f:
         f.write(re.sub(str.encode(r'sig="(?P<sig>.+)"'), sig_repl, contents))

class Test:
   Status = enum('OK', 'FAILED', 'CRASHED', 'TIMEDOUT', 'UNSTABLE', 'SKIPPED')

   def __init__(
         self,
         path                = None,
         script              = None,  # To be set later in prepare_test
         plugin_sources      = '*.c *.cc *.cpp *.cxx',
         program_sources     = '',
         program_name        = 'test',
         output_image        = 'testrender.tif',     ## can be '' if the test does not generate an image
         reference_image     = '',
         progressive         = False,
         kick_params         = '',
         resaved             = None,
         forceexpand         = False,
         scene               = 'test.ass',
         diff_hardfail       = 0.0157,  ## (4/256) oiiotool option --hardfail
         diff_fail           = 0.00001, ## (<1/256) oiiotool option --fail
         diff_failpercent    = 33.334,  ## oiiotool option --failpercent
         diff_warnpercent    = 0.0,     ## oiiottol option --warnpercent
         environment         = [],      ## environment variables set before the script is run
         force_result        = 'OK',
         make_thumbnails     = True     ## unused flag, temporarily restored for fixing broken testsuite
      ):
      ## save the params
      self.testsuite = None
      self.source = os.path.abspath(path)
      self.name = os.path.basename(self.source)
      self.script = script
      self.script_type = None
      self.plugin_sources = plugin_sources
      self.program_sources = program_sources
      self.program_name = program_name
      self.out_image = output_image
      self.ref_image = reference_image
      self.progressive = progressive
      self.kick_params = kick_params
      self.resaved = resaved
      self.forceexpand = forceexpand
      self.scene = scene
      self.diff_hardfail = diff_hardfail
      self.diff_fail = diff_fail
      self.diff_failpercent = diff_failpercent
      self.diff_warnpercent = diff_warnpercent
      self.environment = environment
      self.force_result = force_result

      self.status = None
      self.scons_target = None
      self.run_by_scons = False

   @classmethod
   def load(cls, testsuite, test):
      path = os.path.join(testsuite.source, test)
      if not os.path.exists(path):
         return None
      README = os.path.join(path, 'README')
      params = { 'path': path }
      if not os.path.exists(README):
         return None
      with open(README, 'r') as f:
         readme = f.read()
         index = readme.find('PARAMS:')
         if index != -1:
            params.update(eval(readme[index + 8:], globals(), testsuite.symbols))
      test = cls(**params)
      test.testsuite = testsuite
      return test

   def summary(self):
      with open(os.path.join(self.source, 'README'), 'r') as f:
         summary = f.readline().strip('\n')
      return summary

   # This function is called when the script was not especified (self.script is null)
   def generate_command_line(self):
      params = ['-dw', '-r 160 120', '-bs 16', '-sm lambert', '-sl']

      out_image = self.out_image if self.out_image else 'testrender.tif'
      params += {
         '.exr' : ['-o %s' % out_image],
         '.tif' : ['-o %s' % out_image, '-set driver_tiff.dither false']
      }.get(os.path.splitext(out_image)[1], [])

      if system.is_windows:
         params.append('-nocrashpopup')

      if not self.progressive:
         params.append('-dp')

      if self.kick_params:
         if isinstance(self.kick_params, string_types):
            params.append(self.kick_params)
         else:
            params.extend(self.kick_params)

      if self.resaved:
         resaved_extension = self.resaved if isinstance(self.resaved, str) else 'ass'
         forceexpand = '-forceexpand' if self.forceexpand else ''
         self.script = 'kick %s %s -resave test_resaved.%s\n' % (self.scene, forceexpand, resaved_extension) + ' '.join(['kick test_resaved.{}'.format(resaved_extension)] + params)
      else:
         self.script = ' '.join(['kick %s' % self.scene] + params)

   def prepare(self, path=None, env=None):
      # A source test can be prepared in several ways, depending on wether this
      # is being done by SCons or by a decoupled testsuite script:
      # - (1) SCons can prepare and run a test as we have been doing always: The
      #   README file, and all contents in the "data" folder from the "source"
      #   test folder are copied into the "target" test folder, the shaders and
      #   programs are compiled there, and then the test is executed.
      # - (2) SCons can also just prepare the test and install it in a distributable
      #   testsuite that can be executed then by a decoupled testsuite script.
      #   If will contain the README/ref/data structure, and it will have also
      #   an additional folder "dist" if the test preparation builds programs or
      #   shaders.
      # - (3) If prepared by the decoupled testsuite script, it will expect a
      #   "source" with the structure prepared by (2), which is the same as the
      #   "source" in (1) but with the additional "dist" folder.

      self.target  = os.path.abspath(path if path else os.path.join(self.testsuite.target, self.name))
      source_ref   = os.path.join(self.source, 'ref' )
      source_data  = os.path.join(self.source, 'data')
      source_dist  = os.path.join(self.source, 'dist')
      target_data  = self.target if not self.testsuite.want_to_distribute else os.path.join(self.target, 'data')
      target_dist  = self.target if not self.testsuite.want_to_distribute else os.path.join(self.target, 'dist')

      if os.path.exists(os.path.join(source_data, 'test.cpp')):
         if not self.script:
            self.script_type = 'cpp'
            self.script = os.path.join(self.target, 'test')
         self.program_name    = 'test'
         self.program_sources = 'test.cpp'

      if os.path.exists(os.path.join(source_data, 'test.py')):
         if not self.script:
            self.script_type = 'py'
            self.script = './test.py'

      # If we specified a reference image, then we have to enforce an output
      # image to check with. But if the test was created without a reference,
      # maybe we actually wanted a test without images.
      if not self.ref_image: self.ref_image = None
      if not self.out_image: self.out_image = None
      if (self.ref_image is None) != (self.out_image is None):
         ref_name, out_name = 'reference', 'testrender'
         if not self.ref_image:
            for ext in ['.exr', '.tif']:
               if os.path.exists(os.path.join(source_ref, ref_name + ext)):
                  self.ref_image = os.path.join('ref', ref_name + ext)
                  break
         if not self.out_image or self.out_image == out_name + '.tif':
            self.out_image = out_name + os.path.splitext(self.ref_image)[1] if self.ref_image else None
      self.ref_image = os.path.join(self.source, self.ref_image) if self.ref_image else None
      self.out_image = os.path.join(self.target, self.out_image) if self.out_image else None

      # If an execution command line was not specified or generated, set the default one
      if not self.script:
         self.script_type = 'kick'
         self.generate_command_line()

      # Look for the shader source files, using a list of file or a recursive
      # glob search with a list of patterns
      if self.plugin_sources.find('*') == -1:
         shader_files = [os.path.join(source_data, shader) for shader in self.plugin_sources.split() if shader != 'test.cpp']
      else:
         shader_files = []
         for root, dirs, files in os.walk(source_data):
            for pattern in self.plugin_sources.split():
               shader_files += [f for f in glob.glob(os.path.join(root, pattern)) if not os.path.basename(f) == 'test.cpp']

      self.has_to_build = self.program_sources or shader_files

      if env is None:
         # If there's no SCons environment we won't be able to compile the
         # programs, shaders, etc. So skip the test if it contains such files.
         # But if a "dist" folder is present in the test source path, then it should
         # contain all pre-built binaries needed for running the test.
         if self.has_to_build and not os.path.exists(source_dist):
            return False
         # Clean the target path content
         if os.path.realpath(self.target) != os.path.realpath(self.source):
            if os.path.exists(self.target):
               # In windows, with unicode paths we are getting a WindowsError [123]
               # exception. Let's instruct python to use it properly (see #8511)
               try:
                  self_target = unicode(self.target)
               except NameError as e:
                  self_target = self.target
               shutil.rmtree(self_target)
            os.makedirs(self.target)

      # Install needed source data files such as README, scenes, textures, etc.
      TARGETS = []
      if env:
         TARGETS += env.Install(self.target, os.path.join(self.source, 'README'))
      else:
         shutil.copy2(os.path.join(self.source, 'README'), os.path.join(self.target, 'README'))

      for root, dirs, files in os.walk(source_data):
         for f in files:
            if os.path.splitext(f)[1] in ['.c', '.cpp'] and not self.testsuite.want_to_distribute:
               continue
            d = root
            d = d.replace(source_data, target_data)
            if env:
               TARGETS += env.Install(d, os.path.join(root, f))
            else:
               if not os.path.exists(d):
                  os.makedirs(d)
               shutil.copy2(os.path.join(root, f), os.path.join(d, f))
      for root, dirs, files in os.walk(source_dist):
         src, dst = root, root.replace(source_dist, target_data)
         for f in files:
            if not os.path.exists(dst):
               os.makedirs(dst)
            shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
      if self.testsuite.want_to_distribute:
         for root, dirs, files in os.walk(source_ref):
            for f in files:
               d = root
               d = d.replace(source_ref, os.path.join(self.target, 'ref'))
               TARGETS += env.Install(d, os.path.join(root, f))
      if env:
         # Silence SCons when preparing the test by globally overriding the
         # construction variable PRINT_CMD_LINE_FUNC in this current environment,
         # used in target generation (Program(), SharedLibrary(), Install(), etc.).
         # Do that only when we also want to run the tests from the classic
         # testsuite
         if not self.testsuite.want_to_distribute:
            env['PRINT_CMD_LINE_FUNC'] = lambda s, target, src, env : None
            if 'UNIVERSAL_ENVS' in env:
               for e in env['UNIVERSAL_ENVS']:
                  e['PRINT_CMD_LINE_FUNC'] = lambda s, target, src, env : None
         # Specify the target folder where SCons will build programs/shaders/etc.
         target_build = os.path.join(self.target, 'build')
         env.VariantDir(target_build, source_data, duplicate=0)
         obj_sh_ext = 'obj' if system.is_windows else 'os'
         obj_st_ext = 'obj' if system.is_windows else 'o'
         # Configure SCons builders for compiling shared libraries
         for cpp in shader_files:
            name = os.path.splitext(os.path.basename(cpp))[0]
            if 'UNIVERSAL_ENVS' in env:
               shader_targets = []
               for i, e in enumerate(env['UNIVERSAL_ENVS']):
                  target_build_arch = os.path.join(target_build, e['ARCH'])
                  obj = os.path.join(target_build_arch, '{}.{}'.format(name, obj_sh_ext))
                  shader_obj = e.SharedObject(obj, cpp)
                  shader_target = os.path.join(target_build_arch, name)
                  shader_targets.append(e.SharedLibrary(shader_target, shader_obj)[0])
               shader_target = os.path.join(target_build, shader_targets[0].name)
               shader_target = env.lipo(shader_target, shader_targets)
            else:
               obj = os.path.join(target_build, '{}.{}'.format(name, obj_sh_ext))
               shader_obj = env.SharedObject(obj, cpp)[0]
               shader_target = os.path.join(target_build, name)
               shader_target = env.SharedLibrary(shader_target, shader_obj)[0]
            TARGETS += env.Install(target_dist, shader_target)
         # Configure SCons builder for compiling a program
         if self.program_sources:
            program_source = [os.path.join(source_data, f) for f in self.program_sources.split()]
            if 'UNIVERSAL_ENVS' in env:
               program_targets = []
               for i, e in enumerate(env['UNIVERSAL_ENVS']):
                  target_build_arch = os.path.join(target_build, e['ARCH'])
                  program_objs = []
                  for cpp in program_source:
                     name = os.path.splitext(os.path.basename(cpp))[0]
                     obj = os.path.join(target_build_arch, '{}.{}'.format(name, obj_st_ext))
                     program_objs.extend(e.Object(obj, cpp))
                  program_target = os.path.join(target_build_arch, self.program_name)
                  program_targets.append(e.Program(program_target, program_objs)[0])
               program_target = os.path.join(target_build, program_targets[0].name)
               program_target = env.lipo(program_target, program_targets)
            else:
               program_objs = []
               for cpp in program_source:
                  name = os.path.splitext(os.path.basename(cpp))[0]
                  obj = os.path.join(target_build, '{}.{}'.format(name, obj_st_ext))
                  program_objs.extend(env.Object(obj, cpp))
               program_target = os.path.join(target_build, self.program_name)
               program_target = env.Program(program_target, program_objs)[0]
            TARGETS += env.Install(target_dist, program_target)
         if self.testsuite.want_to_distribute:
            self.scons_target = TARGETS
         else:
            # SCons builder that will run the test and produce the JSON data
            self.scons_target = env.run_test(
               os.path.join(self.target, self.name + '.json'), TARGETS,
               TEST_OBJECT = self,
               PRINT_CMD_LINE_FUNC = lambda a, b, c, d : None # silence the builder
            )
            self.run_by_scons = True
            # Force SCons to always build the JSON file, and give it an alias
            env.AlwaysBuild(self.scons_target)
         env.Alias(self.name, self.scons_target)

      return True

   @staticmethod
   def status(process):
      '''Translates the return code of the process (as obtained by os.system or subprocess) into a status enum
      '''
      if process.timeout:
         return Test.Status.TIMEDOUT

      if process.returncode == 0:
         return Test.Status.OK

      if not system.is_windows:
         # In Linux/OSX, when a signal "N" is raised, the process can exit with
         # code "128 + N" or "-N" (http://tldp.org/LDP/abs/html/exitcodes.html)
         if process.returncode < 0 or process.returncode > 128:
            return Test.Status.CRASHED
      else:
         # The exit code returned by WinAPI GetExitCodeProcess() is a DWORD
         # (i.e. a 32-bit unsigned integer). Related functions also use an
         # unsigned 32-bit integer, such as ExitProcess, GetExitCodeThread, etc.
         # Starting with Python 3.3, subprocess.Popen returns the exit code as
         # an unsigned integer
         retcode = process.returncode + (1 << 32) if process.returncode < 0 else process.returncode
         if retcode >= 0xC0000000:
            return Test.Status.CRASHED

      return Test.Status.FAILED

   def run(self, timeout=0, update_passes=[]):
      epoch = time.time()

      # Run all the requested passes, storing the status for every pass
      # Skip the passes where the test is in its skipped list
      passes_skipped = {k[5:]: self.testsuite.skipped_tests[k] for k in self.testsuite.skipped_tests if k.startswith('pass_')}
      passes = self.testsuite.passes
      passes = [_ for _ in passes if self.name not in passes_skipped.get(_, [])]
      pass_status = {k: Test.Status.OK for k in passes}

      # #remove any leftovers
      for pass_name in passes:
         path.remove(os.path.join(self.target, 'testrender.{}.tif'.format(pass_name)))
         path.remove(os.path.join(self.target, 'ref.{}.png'.format(pass_name)))
         path.remove(os.path.join(self.target, 'out.{}.png'.format(pass_name)))

      # This assumes there are no tests that have the following extensions as inputs
      for file_to_remove in glob.iglob(os.path.join(self.target, 'dif.*pu.png')):
         os.remove(file_to_remove)
      for file_to_remove in glob.iglob(os.path.join(self.target, 'test_*.log')):
         os.remove(file_to_remove)
      for file_to_remove in glob.iglob(os.path.join(self.target, 'test_*.json')):
         os.remove(file_to_remove)
      for file_to_remove in glob.iglob(os.path.join(self.target, 'test_*.html')):
         os.remove(file_to_remove)

      # This dictionary will collect all data that we want to report from the
      # test execution
      test = {
         'name' : self.name,
         'epoch': datetime.utcnow().strftime('%Y%m%d%H%M%S.%f')
      }
      # Print the test output if only one is going to be executed
      show_output = len(self.testsuite.tests_prepared) == 1
      # Clone the environment variables defined for the testsuite
      environment_global = dict(self.testsuite.environment)
      # Force RLM to log debug and diagnostics (see #7833).
      environment_global['RLM_DIAGNOSTICS'] = os.path.join(self.target, 'rlm.diag')
      environment_global['RLM_DEBUG'] = ''
      # Force CLM Hub to write the log files in the test working directory and
      # raise the log level to "trace" in order to get the full verbosity
      # (see #5658).
      for clm_version in ['1', '2']:
         clm_prefix = {'1': 'ADCLMHUB', '2': 'ADLSDK'}.get(clm_version)
         environment_global['{}_LOG_DIR'.format(clm_prefix)]   = self.target
         environment_global['{}_LOG_LEVEL'.format(clm_prefix)] = 'T'
      # Finally, override the environment with specific variables in the test
      # Since we are working with copies of the original os.environ, and windows
      # is not case sensitive (all env vars are uppercase), let's transform the
      # test environment accordingly
      if self.environment:
         environment_global.update(
            {key.upper(): value for key, value in self.environment}
            if system.is_windows else
            dict(self.environment)
         )
      # Construct the paths to ref/out usd/hydra images in a dictionary, so that
      # we can better generalize some processings. We will be able to reference
      # them as images[('ref', pass)] or images[('out', pass)]. At this point
      # self.ref_image and self.out_image ar both or None, or they have a value
      images = {}
      epoch_commands = time.time()
      for pass_name in passes:
         # Specialize the environment per pass, cloning the global environment
         environment = dict(environment_global)
         environment['ARNOLD_TESTSUITE_PASS'] = pass_name
         environment['PROCEDURAL_USE_HYDRA'] = 1 if pass_name == 'hydra' else 0
         
         # Construct the path to the reference image of the current pass. If
         # there's no specific reference image for this pass (which should end
         # with ".<pass>.<ext>"), use the global reference. And if there's no
         # reference, that means that this test won't check diffs.
         images[('ref', pass_name)] = None
         if self.ref_image and os.path.exists(self.ref_image):
            ref_img = '{1}.{0}{2}'.format(pass_name, *os.path.splitext(self.ref_image))
            images[('ref', pass_name)] = ref_img if os.path.exists(ref_img) else self.ref_image
         # Run the list of commands in the script
         log_outputs = []
         for command in self.script.splitlines():
            # Commands marked with the character '$' will be executed
            # as shell bultins. We should use that for copy, move, etc in the
            # windows platform (see #4085)
            use_shell = system.is_windows
            if command[0] == '$':
               command = command[1:]
               use_shell = True
            # Ignore './' in windows, since it's not needed and it doesn't
            # understand it.
            if command.startswith('./') and system.is_windows:
               command = command[2:]
            # Get the first token from the command line. It will be the program
            # we want to execute.
            token = command.split()[0]
            if token.endswith('.py'):
               # If the first token in the command is a python script, then use the
               # interpreter we used for launching the current script
               command = '{} {}'.format(sys.executable, command)
            elif token == 'kick' and self.testsuite.kick_params:
               # If the token is "kick" and the user provided additional params,
               # let's append them to the command line
               command = '{} {}'.format(command, self.testsuite.kick_params)
            # Execute the command
            completed_process = system.execute(command, cwd=self.target, env=environment, shell=use_shell, timeout=timeout, verbose=show_output, version=2)
            obfuscate_log(environment['RLM_DIAGNOSTICS'])
            # Output into common log
            log_outputs += [f'Executing {command}']
            log_outputs += completed_process.stdout
            log_outputs += ['']
            # Process the return status of the command
            pass_status[pass_name] = Test.status(completed_process)
            # If the command failed, just stop the current pass
            if pass_status[pass_name] != Test.Status.OK:
               break

         # Output into common log
         logFilePath = os.path.join(self.target, '{}.{}.log'.format(self.name, pass_name))
         with open(logFilePath, 'w') as logFile:
            logFile.write('\n'.join(log_outputs))
         # Obfuscate any sensible info found in the logs
         obfuscate_log(logFilePath)

         # Rename the output image (if generated) so that we don't overwrite things
         # NOTE: os.rename() fails in windows when the destination file exists.
         # Let's use the an equivalent statements sequence
         images[('out', pass_name)] = None
         if self.out_image and os.path.exists(self.out_image):
            out_img = '{1}.{0}{2}'.format(pass_name, *os.path.splitext(self.out_image))
            if self.run_by_scons and system.is_windows:
               subprocess.check_output(['move', '/Y', self.out_image, out_img], stderr=subprocess.STDOUT, shell=True)
            else:
               shutil.move(self.out_image, out_img)
            images[('out', pass_name)] = out_img
         # Mark the pass as FAILED if we have a reference, but it didn't generate
         # an output image.
         if images[('ref', pass_name)] and not images[('out', pass_name)]:
            if pass_status[pass_name] == Test.Status.OK:
               pass_status[pass_name] = Test.Status.FAILED
      self.status = max(pass_status.values()) if len(pass_status) else Test.Status.SKIPPED
      test['duration'] = time.time() - epoch_commands

      for pass_name in (p for p in update_passes if p in passes and pass_status[p] == Test.Status.OK):
         shutil.copy2(images[('out', pass_name)], images[('ref', pass_name)])
         if pass_name == 'usd':
            src_log = os.path.join(self.target, '{}.{}.log'.format(self.name, pass_name))
            dst_log = os.path.join(self.source, 'ref', 'reference.log')
            shutil.copy2(src_log, dst_log)

      # if output is deep and determine which channels to display
      info = {}
      for pass_name in passes:
         info[pass_name] = {'is_deep': False, 'channels': 'R,G,B', 'alpha': 'A'}
         ref_img = images[('ref', pass_name)]
         if ref_img and os.path.exists(ref_img):
            cmd = [self.testsuite.tools['oiiotool'], '-v', '--info', '--wildcardoff', ref_img]
            error, output = system.execute(cmd, cwd=self.target, env=environment)
            if not error:
               if 'subimages' in output[2]:
                  output.pop(2)
               info[pass_name]['is_deep'] = 'deep' in output[1].split(',')[-1]
               channels_found = [c.strip().split(' ')[0] for c in output[2].split(': ')[1].split(',')]
               channels_goal  = ['R', 'G', 'B', 'A']
               new_channels   = [None] * len(channels_goal)
               for channel_found in channels_found:
                  for i, channel_goal in enumerate(channels_goal):
                     if channel_found[-1] == channel_goal and channel_found != 'A' + channel_goal:
                        if not new_channels[i]:
                           new_channels[i]  = channel_found
               if new_channels[3]:
                  info[pass_name]['alpha'] = new_channels[3]
               if any(channel is None for channel in new_channels[:3]):
                  new_channels = [0, 0, 0]
               info[pass_name]['channels'] = '{},{},{}'.format(*new_channels[:3])
      # Configure the diff thresholds for every pass
      diff_thresholds = {}
      diff_thresholds['usd'] = {
            'hardfail'   : self.diff_hardfail,
            'fail'       : self.diff_fail,
            'failpercent': self.diff_failpercent,
            'warnpercent': self.diff_warnpercent
      }
      diff_thresholds['hydra'] = dict(diff_thresholds['usd'])

      # Configure the diffs that we want to perform
      # NOTE : in arnold-usd we don't want to compare between the 2 passes, cause the reference is always the same
      diff_checks = [
         [('out', 'usd'), ('ref', 'usd')],
         [('out', 'hydra'), ('ref', 'hydra')]
      ]
      for keys in diff_checks:
         # Skip the check if a pass generating an output image failed. That
         # means that some of the commands failed or an output image was
         # expected but not generated
         if any(pass_status.get(p) != Test.Status.OK for t, p in keys if t == 'out'):
            continue
         out_key , ref_key  = keys
         out_type, out_pass = out_key
         ref_type, ref_pass = ref_key
         out_img = images.get(out_key, None)
         ref_img = images.get(ref_key, None)
         all_img = out_img, ref_img
         # Skip the check if we don't have a full out/ref tuple
         if not (out_img and ref_img):
            continue
         # We want a hard fail if we are comparing the output vs the reference
         # images of the same pass
         hard_fail = (out_pass == ref_pass) and (set((out_type, ref_type)) == set(('out', 'ref')))
         # Generate the base diff command using oiiotool
         diff_cmd = [self.testsuite.tools['oiiotool'], '--threads', '1', '-a']
         # Generate the oiiotool flags for specifying the desired thresholds
         # in the diff. This is done by interleaving the keys and values of
         # the diff_thresholds dictionary, previously performing a string formatting
         lists = [
            ['--{}'.format(x) for x in diff_thresholds[out_key[1]].keys()  ],
            [  '{}'.format(x) for x in diff_thresholds[out_key[1]].values()],
         ]
         diff_cmd += [x for t in zip(*lists) for x in t]
         # Disable numeric wildcard expansion for subsequent command line arguments.
         # This is useful when we have filenames or other argumenst that must actually contain '#' or '@'
         # characters. (eg. in Jenkins, the workspace sometimes has a trailing "@N" substring, see ARNOLD-15039)
         diff_cmd += ['--wildcardoff']
         # The actual diff operation in oiiotool, specifying the two images
         # that we want to compare
         diff_cmd += ['--diff', out_img, ref_img]
         # For generating the diff image, we will compute it by substracting
         # the images, taking the absolute value, and multiplying by 8
         diff_img = 'dif.{out[0][0]}{out[1]}-{ref[0][0]}{ref[1]}.png'.format(out=out_key, ref=ref_key)
         if info[out_pass]['is_deep']:
            diff_cmd += ['--flatten', '--swap', '--flatten', '--swap']
         diff_cmd += ['--sub', '--abs', '--cmul', '8', '-ch', '{channels},{alpha}'.format(**info[out_pass])]
         diff_cmd += ['--dup', '--ch', '{alpha},{alpha},{alpha},0'.format(**info[out_pass]), '--add', '-ch', '0,1,2']
         diff_cmd += ['--subimage', '0', '-o', diff_img]
         # Execute the diff command
         error, output = system.execute(diff_cmd, cwd=self.target, verbose=show_output)
         # Append the output of the command to the log file
         with open(os.path.join(self.target, self.name + '.diff.log'), 'a') as f:
            f.write('DIFF {}_{}-{}_{}\n'.format(out_pass, out_type, ref_pass, ref_type))
            f.write('\n'.join(output) + '\n')
            f.flush()
         if error and hard_fail:
            self.status = Test.Status.FAILED

      # Compute the final status of the test taking into account "force_result",
      # flipping the interpretation accordingly
      if Test.Status.enum.get(self.force_result) != Test.Status.OK:
         if self.status == Test.Status.enum.get(self.force_result):
            self.status = Test.Status.OK
         elif self.status == Test.Status.OK:
            self.status = Test.Status.FAILED
      test['result'] = self.status

      # Dump all collected data from the test execution into a JSON file
      with open(os.path.join(self.target, self.name + '.json'), 'w') as f:
         json.dump(test, f)

      # Generate the out/ref per pass images which are suitable for use in HTML
      # reports
      for pass_name in passes:
         for pass_type in ['out', 'ref']:
            img = images.get((pass_type, pass_name), None)
            if img is None:
               continue
            # Generate the base diff command using oiiotool
            thumb_cmd  = [self.testsuite.tools['oiiotool'], '--threads', '1']
            # Disable numeric wildcard expansion for subsequent command line arguments.
            # This is useful when we have filenames or other argumenst that must actually contain '#' or '@'
            # characters. (eg. in Jenkins, the workspace sometimes has a trailing "@N" substring, see ARNOLD-15039)
            thumb_cmd += ['--wildcardoff']
            thumb_cmd += [img]
            if info[pass_name]['is_deep']:
               thumb_cmd += ['--flatten']
            thumb_cmd += ['--ch', info[pass_name]['channels']]
            thumb_cmd += ['-o', '{}.{}.png'.format(pass_type, pass_name)]
            # Execute the thumbnail genration command
            error, output = system.execute(thumb_cmd, cwd=self.target)
      # Get the HTML template
      with open(os.path.join(os.path.dirname(__file__), '__resource__', 'test.html.template')) as f:
         html_template = f.read()
      # Get README so that we can stick it inside the HTML file
      with open(os.path.join(self.target, 'README'), 'r') as f:
         readme = f.read()
      # Create the HTML file with the results
      with open(os.path.join(self.target, self.name + '.html'), 'w') as f:
         params = {
            'name'      : self.name,
            'status'    : Test.Status.name[self.status],
            'readme'    : readme,
            'new_image' : os.path.exists(os.path.join(self.target, 'out.usd.png')) and '<div id="thumbnail"><a href="out.usd.png" target="_blank"><img src="out.usd.png" style="padding: 0 2px 0 2px;" border="0" hspace="0" width="160" height="120" alt="new image" title="new image (opens in a new tab)"/></a></div><img id="previewImage" src="out.usd.png"/>' or '&nbsp;',
            'ref_image' : os.path.exists(os.path.join(self.target, 'ref.usd.png')) and '<div id="thumbnail"><a href="ref.usd.png" target="_blank"><img src="ref.usd.png" style="padding: 0 2px 0 2px;" border="0" hspace="0" width="160" height="120" alt="ref image" title="ref image (opens in a new tab)"/></a></div><img id="previewImage" src="ref.usd.png"/>' or '&nbsp;',
            'diff_image': os.path.exists(os.path.join(self.target, 'dif.ousd-rusd.png')) and '<div id="thumbnail"><a href="dif.ousd-rusd.png" target="_blank"><img src="dif.ousd-rusd.png" style="padding: 0 2px 0 2px;" border="0" hspace="0" width="160" height="120" alt="difference image" title="difference image (opens in a new tab)"/></a></div><img id="previewImage" src="dif.ousd-rusd.png"/>' or '&nbsp;<b>no difference</b>&nbsp;'
         }
         f.write(html_template.format(**params))
