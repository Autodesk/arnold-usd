import io
import os
import sys
import re
import shutil
import time
import json
import tarfile
import textwrap
from collections import defaultdict
from xml.sax.saxutils import escape as xml_sax_saxutils_escape

from .test   import Test
from .groups import Groups
from .       import worker

from utils import system

def chunk_list(L, i, n):
   '''split a list into "n" evenly sized chunks, and return the "i-th" chunk'''
   if n > len(L): return []
   if i < 1 or n < 1 or n - i < 0: return []
   i -= 1
   k, m = divmod(len(L), n)
   return L[i * k + min(i, m):(i + 1) * k + min(i + 1, m)]


def xml_escape(data):
   # xml.sax.saxutils.escape() only escapes <, > and & by default
   return xml_sax_saxutils_escape(data, entities={
      "'" : "&apos;",
      "\"": "&quot;",
   })

def _run_test(test, timeout, reruns, update_passes=[]):
   test_target_fileobj = None # archived test worspace in memory
   statuses = []              # statuses of every test run
   for i in range(1 + reruns):
      # Run the test
      test.run(timeout, update_passes)
      # Print the test status
      status_str = ' {{:>{}}}: {{}}'.format(max(len(_) for _ in Test.Status.name.values()))
      system.print_safe(test.name + (status_str.format(
         Test.Status.name[test.status],
         test.summary()
      ) if test.status != Test.Status.OK else ''), flush=True)
      # Stop if the test passed (or it was skipped) in the first run
      # If it failed on the first run, and we enabled reruns to detect
      # instability, archive the current worspace to report it later.
      # Do all this only on the first run.
      if i == 0:
         if test.status in [Test.Status.OK, Test.Status.SKIPPED]:
            break
         elif reruns:
            # archive test.target folder in memory
            test_target_fileobj = io.BytesIO()
            with tarfile.open(fileobj=test_target_fileobj, mode='w:gz', compresslevel=9) as f:
               f.add(os.path.join(test.target, ''), arcname='')
            test_target_fileobj.seek(0)
      statuses.append(test.status)
   # If the test was rerun (since it failed in the first run),
   # check if it is unstable (eg. it passed at least once).
   # If so, we need to amend the generated status JSON file
   if reruns and len(statuses) and any(status == Test.Status.OK for status in statuses):
      # Restore the test workspace from memory for the first failed run
      if os.path.exists(test.target):
         shutil.rmtree(test.target)
      os.makedirs(test.target)
      with tarfile.open(fileobj=test_target_fileobj, mode='r:gz') as f:
         f.extractall(test.target)
      # Amend the test status, setting it as UNSTABLE
      test.status = Test.Status.UNSTABLE
      test_json = os.path.join(test.target, test.name + '.json')
      with open(test_json, 'r') as f:
         test_data = json.load(f)
      test_data['result'] = test.status
      with open(test_json, 'w') as f:
         json.dump(test_data, f)

   # Close the stream if we archived the test workspace in memory
   if test_target_fileobj:
      test_target_fileobj.close()

class Testsuite(object):
   def __init__(self, path, tools, passes):
      self.source  = os.path.abspath(path)
      self.groups  = Groups(os.path.join(self.source, 'groups'))
      self.common  = os.path.join(self.source, 'common')
      self.common  = self.common if os.path.exists(self.common) else None
      self.tools   = {
         'oiiotool' : os.path.abspath(os.path.join(tools['arnold'], 'bin', 'oiiotool')),
         'maketx'   : os.path.abspath(os.path.join(tools['arnold'], 'bin', 'maketx'  )),
      }
      self.tools.update({k: os.path.abspath(v) for k, v in tools.items()})
      self.symbols = {
         'testsuite_common': self.common,
         'oiiotool_path'   : self.tools['oiiotool'],
         'maketx_path'     : self.tools['maketx'],
         'python_path'     : sys.executable,
      }
      self.symbols.update({k: v for k, v in tools.items() if k not in ['arnold', 'oiiotool', 'maketx']})
      self.timeout = None
      # Load the tests
      root, dirs, files = next(os.walk(self.source))
      self.tests_found    = set(t for t in dirs if t.startswith('test_'))
      self.tests_filtered = set()
      self.tests_ignored  = {}
      self.tests_prepared = []
      self.tests = {}
      self.total_running_time = 0
      self.passes = passes
            
      # Construct the skipped_tests list:
      # - Tests in the 'ignore' and 'ignore_gpu' group
      # - Tests in the 'optix_denoiser' when no GPU is detected
      # - Tests in the 'ignore_pre_turing_gpu' group when a "pre-turing" GPU detected
      # - Tests intended for other platform
      self.skipped_tests = {}
      self.skipped_tests['ignore'] = self.groups.get_tests('ignore')
      
      self.skipped_tests['os'] = set()
      for system_os in (_ for _ in system.allowed.os if _ != system.os):
         self.skipped_tests['os'] |= (self.groups.get_tests(system_os) - self.groups.get_tests(system.os))
      
      # Configure the environment variables for all the tests
      self.environment = dict(os.environ)
      # Make sure we can find kick, noice, maketx, etc. (PATH) and also the
      # Python API (PYTHON_PATH). Make sure the linked binaries can find libai
      # (LIBRARY_PATH)
      PATH, LIBRARY_PATH, PYTHON_PATH = system.PATH, system.LIBRARY_PATH, 'PYTHONPATH'
      for k, p in zip([PATH, LIBRARY_PATH, PYTHON_PATH], ['bin', 'bin', 'python']):
         self.environment[k] = (os.pathsep + self.environment[k]) if self.environment.get(k) else ''
         self.environment[k] = os.path.join(self.tools['arnold'], p) + self.environment[k]
      # Allow access to some simple tools during testing such as cryptomatte. We
      # have to include 'arnold' subpath, as otherwise there is a clash with
      # 'dist.int/python/arnold/'
      self.environment[PYTHON_PATH] = (
         os.path.realpath(os.path.join(os.path.dirname(__file__), os.pardir, os.pardir, 'arnold')) +
         os.pathsep + self.environment[PYTHON_PATH])
      # Ignore the contents of ARNOLD_PLUGIN_PATH so no custom plugins are
      # loaded by regression tests
      if 'ARNOLD_PLUGIN_PATH' in self.environment:
         del self.environment['ARNOLD_PLUGIN_PATH']
      # Expose the root path of the Arnold SDK as an environment veriable, that
      # can be used by tests and expanded in ass files using [ARNOLD_SDK_PATH]
      self.environment['ARNOLD_SDK_PATH'] = self.tools['arnold']
      # Configure CER to use AUTOSEND mode, which was purposefully designed for use in automated testing
      
      self.report_params = {}
      if 'TESTSUITE_ARNOLD_PLUGIN_PATH' in self.environment:
         self.environment['ARNOLD_PLUGIN_PATH'] = self.environment['TESTSUITE_ARNOLD_PLUGIN_PATH']
      else:
         self.environment['ARNOLD_PLUGIN_PATH'] = os.path.dirname(self.environment['USD_PROCEDURAL_PATH'])
      
      if 'TESTSUITE_PXR_PLUGINPATH_NAME' in self.environment:
         self.environment['PXR_PLUGINPATH_NAME'] = self.environment['TESTSUITE_PXR_PLUGINPATH_NAME']
      else:
         self.environment['PXR_PLUGINPATH_NAME'] = self.environment['PREFIX_RENDER_DELEGATE']

      # Disable ADP in testsuite. On linux there's a 5s delay at exit, which
      # multiplied by a few thousand tests is not OK. The opt-in dialog window
      # might pop up on a fresh machine, which could hang or crash depending on
      # OS (could be fixed by instead force opting in). There's also a bunch of
      # data to upload and it pollutes the stats. Not ideal, but until it works
      # better, let's rely on developers having ADP enabled locally to catch
      # bugs.
      self.environment['ARNOLD_ADP_DISABLE'] = '1'

   @classmethod
   def load(cls, path, tools, passes):
      path = os.path.abspath(path)
      if not os.path.exists(path):
         return None
      return cls(path, tools, passes)

   def tests_by_node(self, node):
      tests = set()
      for test in self.tests_found:
         test_data = os.path.join(self.source, test, 'data')
         if os.path.exists(test_data) and os.path.isdir(test_data):
            root, dirs, files = next(os.walk(test_data))
            for f in files:
               expression = {
                  '.ass': '^[ \t]*{}[ \t]*\n[ \t]*{{'.format(node),
                  '.py' : 'AiNode\s*\(\s*[\"\']{}[\"\']'.format(node),
                  '.cpp': 'AiNode\s*\(\s*\"{}\"'.format(node)
               }.get(os.path.splitext(f)[1], None)
               
               if expression:
                  # We have some .ass files intentionally corrupted with binary
                  # data, so let's open them in binary mode, then decode them
                  # replacing the offending data
                  with open(os.path.join(root, f), 'rb') as file:
                     contents = file.read().decode('utf-8', 'replace')
                     if re.search(expression, contents, re.MULTILINE):
                        tests.add(test)
                        break
      return tests

   def tests_by_status_failed(self):
      tests = set()
      for test in self.tests_found:
         test_json = os.path.join(self.target, test, test + '.json')
         if os.path.exists(test_json) and os.path.isfile(test_json):
            with open(test_json, 'r') as f:
               data = json.load(f)
            if data['result'] != Test.Status.OK:
               tests.add(test)
      return tests

   def filter(self, tests, patterns=None):
      '''Returns the list of tests filtered by patterns, where every token has
         the form expression[,expression,...][:group[,group,...]]. That list is
         finally filtered by the list of test to skip
      '''
      def parse_patterns(patterns):
         parsed_patterns = []
         for pattern in (patterns if patterns else ['testsuite']):
            expressions, separator, groups = pattern.partition(':')
            expressions = set(expressions.split(',') if expressions else ['testsuite'])
            groups      = set(groups.split(',')      if groups      else []           )
            # The "testsuite" expression is equivalent to "test_*" which includes
            # all tests provided for filtering. So it's enough to keep that only
            if 'testsuite' in expressions: expressions = set(['testsuite'])
            parsed_patterns.append([expressions, groups])
         return parsed_patterns

      tests_groups = []
      for expressions, groups in parse_patterns(patterns):
         _tests = set()
         for expression in expressions:
            if expression == 'testsuite':
               _tests |= tests
            elif expression.startswith('test_'):
               expression = expression.replace('*', '.*')
               expression = expression.replace('?', '.')
               regex = re.compile('^' + expression + '$')
               _tests |= set(t for t in tests if re.match(regex, t))
         _groups = None
         for group in groups:
            if _groups is None:
               _groups = set()
            if group == 'failed':
               _groups |= self.tests_by_status_failed()
            else:
               tests_by_group = self.groups.get_tests(group)
               if tests_by_group:
                  _groups |= tests_by_group
               else:
                  _groups |= self.tests_by_node(group)
         tests_groups += [[_tests, _groups]]

      tests_filtered = set()
      for _tests, _groups in tests_groups:
         tests_filtered |= _tests & _groups if _groups is not None else _tests

      return tests_filtered

   def prepare(self, path, patterns, env=None, chunk=[1, 1], order='reverse', timeout=0, update_passes=[], kick_params=None, report_params = []):
      if env is not None:
         # If the user provided a SCons environment, let's configure the needed
         # builders for running the tests and generating the final report
         def action_run_test(target, source, env):
            test = env['TEST_OBJECT']
            _run_test(test, timeout, int(env['TESTSUITE_RERUNS_FAILED']), update_passes)
            return 0
         def action_gen_report(target, source, env):
            testsuite = env['TEST_SUITE']
            testsuite.report_json()
            if 'JUNIT_TESTSUITE_NAME' in env:
                testsuite_url = env['JUNIT_TESTSUITE_URL'] if 'JUNIT_TESTSUITE_URL' in env else None
                testsuite.report_junit_xml(env['JUNIT_TESTSUITE_NAME'], testsuite_url)
            report_only_fail = env['REPORT_ONLY_FAILED_TESTS'] if 'REPORT_ONLY_FAILED_TESTS' in env else false
            testsuite.report_html(only_failed_tests=report_only_fail)
            system.print_safe(testsuite.report_text(), flush=True)
            system.print_safe('View testsuite results at: file://{}'.format(target[0].abspath), flush=True)
            return 1 if testsuite.failed(float(env['TESTSUITE_INSTABILITY_THRESHOLD'])) else 0
         env.Append(BUILDERS = {
            'run_test'  : env.Builder(action = env.Action(action_run_test  )),
            'gen_report': env.Builder(action = env.Action(action_gen_report)),
         })
      self.target = os.path.abspath(path)
      self.want_to_distribute = env is not None and 'TESTSUITE_PREFIX' in env
      self.report_params = report_params
      # If we are distributing the testsuite or running it with the decoupled
      # script, let's clear the output folder
      if os.path.exists(self.target) and (env is None or self.want_to_distribute):
         shutil.rmtree(self.target)
      if not os.path.exists(self.target):
         os.makedirs(self.target)
      self.kick_params = kick_params
      # Create and enable the Optix cache through its environment variable
      optix_cache_path = os.path.join(self.target, '.nv')
      if not os.path.exists(optix_cache_path):
         try:
            os.makedirs(optix_cache_path)
         except OSError as e:
            # There is a chance that the folder was created by another
            # process that also passed that existence check. If so, "pass"
            # the exception, raise the error again otherwise. (see #8301)
            if e.errno != errno.EEXIST: raise
      self.environment['OPTIX_CACHE_PATH'] = optix_cache_path
      # Get the requested tests among all the tests found, using the expresions
      # and groups specified in the patterns
      self.tests_filtered = self.filter(self.tests_found, patterns)
      # Filter out tests which are present in the skipped test lists
      self.tests_ignored = {}
      self.tests_ignored['os'] = self.tests_filtered & self.skipped_tests['os']
      self.tests_filtered -= self.skipped_tests['os']
      self.tests_ignored['ignore'] = set()
      if len(self.tests_filtered) > 1:
         self.tests_ignored['ignore'] = self.tests_filtered & self.skipped_tests['ignore']
         self.tests_filtered -= self.skipped_tests['ignore']

      ''' SEB
      self.tests_ignored['pass_cpu'] = self.tests_filtered & self.skipped_tests['pass_cpu']
      self.tests_ignored['pass_gpu'] = self.tests_filtered & self.skipped_tests['pass_gpu']
      '''
      self.tests_ignored['load']  = set()
      self.tests_ignored['build'] = set()
      self.tests_ignored['other'] = set()
      self.tests = {}
      self.tests_prepared = []
      def test_key(name):
          try:    return float(name[5:]) # strip 'test_' prefix, convert rest to float
          except: return 0.0
      keys = chunk_list(sorted(self.tests_filtered, key=test_key, reverse=(order=='reverse')), *chunk)
      for key in keys:
         test = Test.load(self, key)
         if not test:
            self.tests_ignored['load'].add(key)
            continue
         self.tests[key] = test
         test_target = os.path.join(self.target, key)
         if self.tests[key].prepare(test_target, env):
            self.tests_prepared.append(key)
         elif self.tests[key].has_to_build:
            self.tests_ignored['build'].add(key)
         else:
            self.tests_ignored['other'].add(key)
      if env is not None:
         TARGETS = [self.tests[key].scons_target for key in self.tests_prepared]
         if self.want_to_distribute:
            TARGETS += env.Install(self.target, os.path.join(self.source, 'groups'))
            for root, dirs, files in os.walk(self.common):
               src, dst = root, root.replace(self.common, os.path.join(self.target, 'common'))
               for f in files:
                  TARGETS += env.Install(dst, os.path.join(src, f))
            env.Alias('testsuite_distribution', TARGETS)
            TARGETS = ['testsuite_distribution']
         elif len(self.tests_prepared) == 1:
            env.AlwaysBuild(TARGETS)
         elif len(self.tests_prepared) > 1:
            # SCons builder that will run the testsuite and produce the JSON/HTML data
            TARGETS = env.gen_report(
               os.path.join(self.target, 'index.html'), TARGETS,
               TEST_SUITE = self,
               PRINT_CMD_LINE_FUNC = lambda a, b, c, d : None # silence the builder
            )
            env.AlwaysBuild(TARGETS)
         return env.Flatten(TARGETS)
      else:
         return [self.tests[key] for key in self.tests_prepared]

   def run(self, workers=1, timeout=0, reruns=1):
      epoch = time.time()
      pool = worker.Pool(workers)
      for test in (self.tests[key] for key in self.tests_prepared):
         pool.add_task(_run_test, test, timeout, reruns)
      pool.wait_completion()
      self.total_running_time = time.time() - epoch

   def failed(self, instability_threshold=0.0):
      result = self.report()
      total_tests = sum(result[key] for key in ['passed', 'failed', 'crashed', 'timedout', 'unstable'])
      total_failed = total_tests - result['passed']
      total_failed_stable = total_failed - result['unstable']
      max_failed_unstable = (float(instability_threshold) * float(total_tests)) / 100.0

      return (total_failed_stable > 0) or (float(result['unstable']) > max_failed_unstable)

   def report(self):
      result = {key: 0 for key in ('total', 'passed', 'failed', 'crashed', 'timedout', 'unstable', 'skipped')}
      tests              = [self.tests[key] for key in self.tests_prepared]
      result['total']    = len(tests)
      result['passed']   = len([test for test in tests if test.status == Test.Status.OK      ])
      result['failed']   = len([test for test in tests if test.status == Test.Status.FAILED  ])
      result['crashed']  = len([test for test in tests if test.status == Test.Status.CRASHED ])
      result['timedout'] = len([test for test in tests if test.status == Test.Status.TIMEDOUT])
      result['unstable'] = len([test for test in tests if test.status == Test.Status.UNSTABLE])
      skipped_keys = ('os', 'ignore', 'load', 'build', 'other')
      skipped = {'skipped_' + key : len(self.tests_ignored[key])  for key in skipped_keys}
      result['skipped']  = sum(skipped.values())
      result.update(skipped)
      return result

   def report_text(self):
      # Report results in a text string
      result = self.report()
      result_string = f'Ran {result["total"]} regression tests'
      if result['skipped'] > 0:
         result_string += f' ({result["skipped"]} skipped)'

      fail_keywords = ['failed', 'crashed', 'timedout', 'unstable']
      result_string_details = []
      if sum(result[key] for key in fail_keywords) == 0:
         result_string_details.append('ALL TESTS OK')
      else:
         for key in fail_keywords:
            count = result[key]
            result_string_details.append(f'{count} {key}')
      return f'{result_string} - {", ".join(result_string_details)}'

   def report_html(self, only_failed_tests=False):
      from utils.contrib.bottle import SimpleTemplate

      result = self.report()
      report_params = {
         'project':           '',
         'arnold_version':    '',
         'revision':          '',
         'repo_url':          '',
         'custom1_name':      '',
         'custom1_value':     '',
         'custom2_name':      '',
         'custom2_value':     '',
         'custom3_name':      '',
         'custom3_value':     '',
         'patterns':          '',
         'tags':              '',
         'total':             result['total'],
         'passed':            result['passed'],
         'failed':            result['failed'],
         'crashed':           result['crashed'],
         'timedout':          result['timedout'],
         'unstable':          result['unstable'],
         'skipped':           result['skipped'],
         'skipped_ignored':   result['skipped_ignore'],
         'skipped_os':        result['skipped_os'],
         'skipped_other':     result['skipped_other'],
         'total_time':        self.total_running_time,
         'tests' : [],
         'passes': []
      }

      for p in self.report_params:
         report_params[p] = self.report_params[p]


      pass_info = {
         'usd': {
            'name': 'USD',
            'images': [
               {'class_name': 'new', 'title': 'New', 'key': 'new_img_usd'},
               {'class_name': 'ref', 'title': 'Ref', 'key': 'ref_img_usd'},
               {'class_name': 'dif', 'title': 'Dif (4x)', 'key': 'dif_img_usd_out-usd_ref'}
            ]
         },
         'hydra': {
            'name': 'Hydra',
            'images': [
               {'class_name': 'new', 'title': 'New', 'key': 'new_img_hydra'},
               {'class_name': 'ref', 'title': 'Ref', 'key': 'ref_img_hydra'},
               {'class_name': 'dif', 'title': 'Dif (4x)', 'key': 'dif_img_hydra_out-hydra_ref'},
            ]
         },
      }

      for pass_name in self.passes:
         if pass_info[pass_name]:
            report_params['passes'].append(pass_info[pass_name])
         else:
            print('ERROR: Could not generate html report for unknown pass "{}"'.format(pass_name))

      tests = [self.tests[key] for key in self.tests_prepared]
      for test in tests:
         # Read the JSON file with all the data collected from the test execution
         test_data_file = os.path.join(test.target, test.name + '.json')
         data = {}
         if os.path.exists(test_data_file):
            with open(test_data_file, 'r') as f:
               data = json.load(f)
         if only_failed_tests and data.get('result') in [Test.Status.OK, Test.Status.SKIPPED]:
            shutil.rmtree(test.target)
            continue
         # Read the README file with the summary and description of the test
         with open(os.path.join(test.target, 'README'), 'r') as f:
            summary = f.readline()
            description = f.read()

         diff_logs = {}
         diff_path = os.path.join(test.target, test.name + '.diff.log')
         if os.path.exists(diff_path):
            with open(diff_path, 'r') as f:
               diff_log = ""
               current_diff_name = ""
               for line in f:
                  if line.startswith('DIFF '):
                     current_diff_name = line[5:].rstrip()
                  else:
                     diff_log += line
                     if line.startswith('PASS') or line.startswith('FAILURE') or line.startswith('WARNING') :
                        diff_logs[current_diff_name] = diff_log
                        diff_log = ""
                        current_diff_name = ""

         images = {}
         for pass_name in self.passes:
            images[('out', pass_name)] = os.path.join(test.name, 'out.{0}.png'.format(pass_name))
            images[('ref', pass_name)] = os.path.join(test.name, 'ref.{0}.png'.format(pass_name))
            images[('dif', pass_name)] = os.path.join(test.name, 'dif.o{0}-r{0}.png'.format(pass_name))

         report_params_test = {
            'name':          test.name,
            'url':           os.path.join(test.name, test.name + '.html'),
            'descr_summary': summary,
            'descr_details': description,
            'status':        {
               Test.Status.OK: 'passed',
               Test.Status.FAILED: 'failed',
               Test.Status.CRASHED: 'crashed',
               Test.Status.TIMEDOUT: 'timedout',
               Test.Status.UNSTABLE: 'unstable',
               Test.Status.SKIPPED: 'skipped' }.get(data.get('result'), 'unknown'),
            'time':          data.get('duration', 0)
         }

         for pass_name in self.passes:
            diff_key = '{0}_out-{0}_ref'.format(pass_name)
            report_params_test.update({
               'new_img_{}'.format(pass_name): os.path.exists(os.path.join(self.target, images[('out', pass_name)])) and images[('out', pass_name)] or '',
               'ref_img_{}'.format(pass_name): os.path.exists(os.path.join(self.target, images[('ref', pass_name)])) and images[('ref', pass_name)] or '',
               'dif_img_{}'.format(diff_key): os.path.exists(os.path.join(self.target, images[('dif', pass_name)])) and images[('dif', pass_name)] or '',
               'dif_img_{}_log'.format(diff_key): diff_logs.get(diff_key, ''),
            })
            if pass_name == 'hydra':
               report_params_test.update({
                  'dif_img_hydra_out-usd_ref': test.name + '/dif.ohydra-rusd.png' if os.path.exists(os.path.join(test.target, 'dif.ohydra-rusd.png')) else '',
                  'dif_img_hydra_ref-usd_ref': test.name + '/dif.rhydra-rusd.png' if os.path.exists(os.path.join(test.target, 'dif.rhydra-rusd.png')) else '',
                  'dif_img_hydra_out-usd_ref_log': diff_logs.get('hydra_out-usd_ref', ''),
                  'dif_img_hydra_ref-usd_ref_log': diff_logs.get('hydra_ref-usd_ref', ''),
               })

         report_params['tests'].append(report_params_test)

      template_filename = 'testsuite.html.template'

      # Read the HTML template
      with open(os.path.join(os.path.dirname(__file__), '__resource__', template_filename), 'r') as f:
         template = f.read()
      # Write the HTML report
      with open(os.path.join(self.target, 'index.html'), 'w') as f:
         report = SimpleTemplate(template).render(report_params)
         try:
            report = str(report)
         except UnicodeEncodeError:
            # This with be catched in Python 2
            report = report.encode('utf-8')
         f.write(report)

   def report_json(self):
      testsuite_data = []

      for test in [self.tests[key] for key in self.tests_prepared]:
         # Read the JSON file with all the data collected from the test execution
         test_data_file = os.path.join(test.target, test.name + '.json')
         test_data = {}
         if os.path.exists(test_data_file):
            with open(test_data_file, 'r') as f:
               test_data   = json.load(f)
               test_status = Test.Status.name.get(test_data.get('result'))
               if test_status:
                  test_data['result'] = test_status

         if test_data:
            testsuite_data.append(test_data)

      # Write the JSON report
      with open(os.path.join(self.target, 'report.json'), 'w') as f:
         json.dump(testsuite_data, f, indent=2)
         

   def report_junit_xml(self, testsuite_name, junit_testsuite_url):

      testcases = []
      testcase_status_num = defaultdict(int)

      for test in [self.tests[key] for key in self.tests_prepared]:
         # Read the JSON file with all the data collected from the test execution
         test_data_file = os.path.join(test.target, test.name + '.json')
         data = {}
         if os.path.exists(test_data_file):
            with open(test_data_file, 'r') as f:
               data = json.load(f)
         # Read the README file with the summary and description of the test
         with open(os.path.join(test.target, 'README'), 'r') as f:
            summary = f.readline().strip()
            description = f.read().strip()

         #  Test.Status            JUnit status
         #  -------------------    ------------
         testcase_status = {
            Test.Status.OK       : 'passed'  ,
            Test.Status.FAILED   : 'failure' ,
            Test.Status.CRASHED  : 'error'   ,
            Test.Status.TIMEDOUT : 'error'   ,
            Test.Status.UNSTABLE : 'error'   ,
            Test.Status.SKIPPED  : 'skipped' ,
         }.get(data.get('result'), 'failure')
         testcase_status_num[testcase_status] += 1
         testcase_time = data.get('duration', 0.0)
         testcase_info = f'name="{test.name}" classname="{testsuite_name}" time="{testcase_time}"'

         if testcase_status in ['passed']:
            testcase_node = f'<testcase {testcase_info} />'
         elif testcase_status in ['failure', 'error']:
            testcase_name = f'{test.name} - {summary}'
            testcase_url  = ''
            testcase_href = ''
            if junit_testsuite_url:
               testcase_url  = f'{junit_testsuite_url}/{test.name}/{test.name}.html'
               testcase_href = xml_escape(f'<a href="{testcase_url}">{testcase_name}</a>')
            testcase_name = xml_escape(testcase_name)
            testcase_node = textwrap.dedent(f"""\
               <testcase {testcase_info}>
                  <{testcase_status} message="{testcase_url if testcase_url else testcase_name}" type="{testcase_status}">{testcase_name}</{testcase_status}>
                  <properties>
                     <property name="name" value="{testcase_name}" />
                     <property name="link" value="{testcase_href}" />
                  </properties>
               </testcase>""")
         else:
            testcase_node = textwrap.dedent(f"""\
               <testcase {testcase_info}>
                  <{testcase_status} />
               </testcase>""")
         testcases.append(testcase_node)

      with open(os.path.join(self.target, f'{testsuite_name}.xml'), 'w') as f:
         f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
         f.write('<testsuites>\n')
         testsuite_tests      = sum(testcase_status_num.values())
         testsuite_failures   = testcase_status_num['failure']
         testsuite_errors     = testcase_status_num['error']
         testsuite_skipped    = testcase_status_num['skipped']
         f.write(f'<testsuite name="{testsuite_name}" tests="{testsuite_tests}" failures="{testsuite_failures}" errors="{testsuite_errors}" skipped="{testsuite_skipped}">\n')
         if junit_testsuite_url:
            f.write('<properties>\n')
            f.write(f'   <property name="link" value="{junit_testsuite_url}/index.html" />\n')
            f.write('</properties>\n')
         f.write('\n'.join(testcases) + '\n')
         f.write('</testsuite>\n')
         f.write('</testsuites>\n')
