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
import os, glob

from SCons.Script import *

from . import system

try:
   # Python 2
   # Empirically, it is faster to check explicitly for str and
   # unicode than for basestring.
   string_types = (str, unicode)
except NameError:
   # Python 3
   string_types = (str)

# Set NOCRASH string for tests which we don't want to debug on crash (Windows only)
NOCRASH = ''
if system.is_windows:
   NOCRASH = '-nocrashpopup'

testsuite_common = os.path.abspath(os.path.join('testsuite', 'common'))

class Test:
   def __init__(self,
                script              = None,  # To be set later in prepare_test
                plugin_sources      = '*.c*',
                program_sources     = None,
                program_name        = 'test',
                plugin_dependencies = '',
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
                make_thumbnails     = True,    ## generate images for html display (needs to be disabled for formats with no conversion to 2D image, like deepexr)
                environment         = [],      ## environment variables set before the script is run
                continue_on_failure = False,   ## if this is a multi-command test, keep running even if one of the early stages fails (crashes)
                force_result        = 'OK'):
      ## save the params
      self.script = script
      self.plugin_sources = plugin_sources
      self.program_sources = program_sources
      self.program_name = program_name
      self.plugin_dependencies = plugin_dependencies
      self.output_image = output_image
      self.reference_image = reference_image
      self.progressive = progressive
      self.kick_params = kick_params
      self.resaved = resaved
      self.forceexpand = forceexpand
      self.scene = scene
      self.diff_hardfail = diff_hardfail
      self.diff_fail = diff_fail
      self.diff_failpercent = diff_failpercent
      self.diff_warnpercent = diff_warnpercent
      self.make_thumbnails = make_thumbnails
      self.environment = environment
      self.continue_on_failure = continue_on_failure
      self.force_result = force_result

   @staticmethod
   def CreateTest(env, test, locals, **kwargs):
      params = dict()
      with open(os.path.join(env.Dir('#').abspath, 'testsuite', test, 'README'), 'r') as f:
         readme = f.read()
         index = readme.find('PARAMS:')
         if index != -1:
            params = eval(readme[index + 8:], globals(), locals)
      params.update(kwargs)
      return Test(**params)

   # This function is called when the script was not especified (self.script is null)
   def generate_command_line(self, test_dir):
      params = ['-dw', '-r 160 120', '-sm lambert', '-bs 16', '-sl']
      
      params += {
         '.exr' : ['-o %s' % self.output_image],
         '.tif' : ['-o %s' % self.output_image, '-set driver_tiff.dither false']
      }.get(os.path.splitext(self.output_image)[1], [])

      params.append(NOCRASH)

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
         self.script = 'kick %s %s -resave test_resaved.%s -db\n' % (self.scene, forceexpand, resaved_extension) + ' '.join(['kick test_resaved.{}'.format(resaved_extension)] + params)
      else:
         renderer = 'kick'
         self.script = ' '.join(['%s %s' % (renderer, self.scene)] + params)

   def prepare_test(self, test_name, env):
      # Silence test preparation by globally overriding the paramater PRINT_CMD_LINE_FUNC
      # in this current SCons sub-environment, used in target generation (Program(),
      # SharedLibrary(), Install(), etc.).
      env['PRINT_CMD_LINE_FUNC'] = lambda s, target, src, env : None

      test_dir       = os.path.join(env.Dir('.').srcnode().abspath, test_name)
      test_data_dir  = os.path.join(test_dir, 'data')
      test_build_dir = os.path.join(env.Dir('.').abspath, test_name)
      reference_dir  = os.path.join(env['REFERENCE_DIR_ROOT'], test_name)

      env.VariantDir(test_build_dir, test_data_dir)

      if os.path.exists(os.path.join(test_data_dir, 'test.cpp')):
         if not self.script:
            self.script = os.path.join(test_build_dir, 'test')
         self.program_name    = 'test'
         # We are checking if the program sources already exists, if it's a list, we append test.cpp, otherwise
         # we expect it to be a string.
         if self.program_sources:
            if isinstance(self.program_sources, list):
               self.program_sources = self.program_sources + ['test.cpp']
            else:
               self.program_sources = [self.program_sources, 'test.cpp']
         else:
            self.program_sources = ['test.cpp']
      
      if os.path.exists(os.path.join(test_data_dir, 'test.py')):
         if not self.script:
            self.script = './test.py'

      # If reference_image was not specified, try to guess from existing files
      if not self.reference_image:
         if os.path.exists(os.path.join(reference_dir, 'ref', 'reference.exr')):
            self.reference_image = os.path.join('ref', 'reference.exr')
            self.output_image    = 'testrender.exr'
         elif os.path.exists(os.path.join(reference_dir, 'ref', 'reference.tif')):
            self.reference_image = os.path.join('ref', 'reference.tif')

      # If an execution command line was not specified or generated, set the default one
      if not self.script:
         self.generate_command_line(test_dir)

      ## process the current test directory
      ## Step 1: build any shaders/procedurals that might exist
      SHADERS = []
      if self.plugin_sources.find('*') == -1:
         ## just a list of regular file names
         shader_files = [os.path.join(test_data_dir, shader) for shader in Split(self.plugin_sources) if shader != 'test.cpp']
      else:
         ## use recursive glob pattern
         shader_files = []
         for root, dirs, files in os.walk(test_data_dir):
            if '.svn' in dirs:
               dirs.remove('.svn')
            for pattern in Split(self.plugin_sources):
               shader_files += [f for f in glob.glob(os.path.join(root, pattern)) if not os.path.basename(f) == 'test.cpp']
      for shader_file in shader_files:
         BUILD_SHADER_FILE = shader_file.replace(test_data_dir, test_build_dir)
         t = env.SharedLibrary(os.path.splitext(BUILD_SHADER_FILE)[0], BUILD_SHADER_FILE)
         SHADERS += t
      if self.program_sources:
         ## we need to build a program
         t = env.Program(os.path.join(test_build_dir, self.program_name), [os.path.join(test_build_dir, f) for f in Split(self.program_sources)])
         SHADERS += t
      FILES = []
      FILES += env.Install(test_build_dir, os.path.join(test_dir, 'README'))

      for root, dirs, files in os.walk(test_data_dir):
         if '.svn' in dirs:
            dirs.remove('.svn')
         for f in files:
            if os.path.basename(f) == 'Makefile':
               continue
            if os.path.splitext(f)[1] == 'c':
               continue
            if os.path.splitext(f)[1] == 'cpp':
               continue
            d = root
            d = d.replace(test_data_dir, test_build_dir)
            FILES += env.Install(d, os.path.join(root, f))

      ## generate the build action that will run the test and produce the html output
      test_target = env.RunTest(os.path.join(test_build_dir, test_name + '.html'), FILES + SHADERS,
         TEST_SCRIPT = self.script,
         REFERENCE_IMAGE = self.reference_image != '' and os.path.join(reference_dir, self.reference_image) or '',
         OUTPUT_IMAGE = self.output_image,
         MAKE_THUMBNAILS = self.make_thumbnails,
         DIFF_HARDFAIL = self.diff_hardfail,
         DIFF_FAIL = self.diff_fail,
         DIFF_FAILPERCENT = self.diff_failpercent,
         DIFF_WARNPERCENT = self.diff_warnpercent,
         FORCE_RESULT = self.force_result,
         CONTINUE_ON_FAILURE = self.continue_on_failure,
         TEST_NAME = test_name,
         ENVIRONMENT = self.environment,
         PRINT_CMD_LINE_FUNC = lambda a, b, c, d : None, ## silence the builder
         chdir = 0)

      env.AlwaysBuild(test_target)
      env.Alias(test_name, test_target)

      return test_target  # The test has been prepared and can be run
