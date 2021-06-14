from __future__ import division
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
from builtins import range
from past.utils import old_div
from builtins import object
import os, shutil, string, platform, subprocess
from os import popen
import time
import shlex, subprocess

from . import path

#
# TimedTest class is a placeholder to store data about a timed test
#
class TimedTest(object):
   def __init__(self, timing = None):
      if timing:
         self.tested  = True
         self._timing = timing
      else:
         self.tested  = False

   def get_timing(self):
      return self._timing

   def set_timing(self, val):
      self.tested  = True
      self._timing = val

   timing = property(get_timing, set_timing)

#
# TimedTestTable behaves like a DB table. It stores in each record a
# testsuite run, along with a timestamp. The table is able to load and
# save its state as a text file.
#
class TimedTestTable(object):
   def __init__(self, base_dir = os.path.abspath('.')):
      self._base_dir   = base_dir
      self._mintest   = 1000000
      self._maxtest   = -1
      self._data      = []
      self._timestamp = []

   def reset(self):
      self._mintest   = 1000000
      self._maxtest   = -1
      self._data      = []
      self._timestamp = []

   # Create a new record in the table, bookmarked with a timestamp
   def timestamp(self, ts = None):
      if ts:
         self._timestamp.append(ts)
      else:
         self._timestamp.append(time.strftime('%Y%m%d%H%M%S', time.gmtime()))
      n_timestamp = len(self._data)
      if n_timestamp == 0:
         self._data.append([])
      elif n_timestamp > 0:
         self._data.append( (self._maxtest + 1) * [ TimedTest() ] )

   # Add a new test with its timing info in the current timestamped record
   def add(self, test, timing):
      if test < self._mintest:
         self._mintest = test
      if test > self._maxtest:
         n_to_add = test - self._maxtest
         for i in range(len(self._data)):
            for j in range(n_to_add):
               self._data[i].append( TimedTest() )
         self._maxtest = test
      self._data[-1][test] = TimedTest(timing)

   # Save the table in a file
   def save(self):
      with open(os.path.join(self._base_dir, 'stats.db'), 'w') as f:
         for i in range(len(self._data)):
            f.write('%s ' % self._timestamp[i])
            for j in range(self._maxtest + 1):
               if self._data[i][j].tested:
                  f.write('%f ' % self._data[i][j].timing)
               else:
                  f.write('X ')
            f.write('\n')

   # Load the table from a file
   def load(self):
      self.reset()
      try:
         with open(os.path.join(self._base_dir, 'stats.db'), 'r') as f:
            file_lines = f.readlines()
            for i in range(len(file_lines)):
               file_lines[i] = file_lines[i].split()
               self.timestamp(file_lines[i][0])
               for j in range(1, len(file_lines[i])):
                  if file_lines[i][j] != 'X':
                     self.add(j-1, float(file_lines[i][j]))
      except IOError:
         return

   # Compute the stats of a given test (from all the execution history)
   def compute_stats(self, test):
      min = 1000000.0
      max = -1000000.0
      sum = 0.0
      mean   = 0.0
      q25    = 0
      median = 0
      q75    = 0
      speedup = 1
      tested = 0
      times = []
      for i in range(len(self._data)):
         if self._data[i][test].tested:
            tm = self._data[i][test].timing
            times.append(tm)
            tested = tested + 1
            sum = sum + tm
            if tm < min:
               min = tm
            if tm > max:
               max = tm
      if tested > 1:
         speedup = old_div(times[-2], times[-1])
      else:
         speedup = 1
      if tested > 0:
         mean = old_div(sum, tested)
         times.sort()
         q25    = times[ int(tested * 0.25) ]
         median = times[ int(tested * 0.5 ) ]
         q75    = times[ int(tested * 0.75) ]

      return (tested, min, max, mean, q25, median, q75, speedup)

   # Generate statistical plots about the testsuite
   def generate_plots(self):
      plot_command = 'gnuplot'
      mintestex = self._maxtest + 1
      maxtestex = 0
      for i in range(self._maxtest + 1):
         if self._data[-1][i].tested:
            if i < mintestex:
               mintestex = i
            if i > maxtestex:
               maxtestex = i

      test_stats = []
      for i in range(self._maxtest + 1):
         test_stats.append(self.compute_stats(i))

      f_filename = os.path.join(self._base_dir, 'plot.data.tmp')
      with open(f_filename, 'w') as f:
         for i in range(self._maxtest + 1):
            tested, min, max, mean, q25, median, q75, speedup = test_stats[i]
            if tested and self._data[-1][i].tested:
               f.write('''%.4d %f %f %f %f %f %f %f %f\n'''   % (i, min, q25, mean, q75, max,   median, speedup, self._data[-1][i].timing))

      # Generate a Histogram (Last Executed Testsuite)
      gnuplot_process = subprocess.Popen(shlex.split(plot_command), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
      gnuplot         = gnuplot_process.stdin

      gnuplot.write('''set xrange [%f:%f]\n''' % (mintestex-0.5, maxtestex+0.5))
      gnuplot.write('''set xtics 0, %d\n''' % (maxtestex - mintestex <= 100 and 10 or 100 ))
      gnuplot.write('''set grid x\n''')
      gnuplot.write('''set grid y\n''')
      gnuplot.write('''set style line 1 lw 1 lc rgb \"#23b323\"\n''')
      gnuplot.write('''set style line 2 lw 1 lc rgb \"#e41919\"\n''')
      gnuplot.write('''set style line 3 lw 1 lc rgb \"#000000\"\n''')
      gnuplot.write('''set style data histogram\n''')
      gnuplot.write('''set style fill solid noborder\n''')
      gnuplot.write('''set boxwidth 0.95\n''')
      gnuplot.write('''set terminal png size 1024,400\n''')
      gnuplot.write('''set output \'%s\'\n''' % os.path.join(self._base_dir, 'plot_rt-sup.png'))

      gnuplot.write('''set multiplot\n''')

      gnuplot.write('''set nokey\n''')
      gnuplot.write('''set xlabel\"\"\n''')
      gnuplot.write('''set ylabel\"Running time (seconds)\"\n''')
      gnuplot.write('''set ytics add (\"\" 0)\n''')
      gnuplot.write('''set size    1, 0.5\n''')
      gnuplot.write('''set origin  0, 0.5\n''')
      gnuplot.write('''set bmargin 0\n''')
      gnuplot.write('''set lmargin 10\n''')
      gnuplot.write('''set rmargin 2\n''')
      gnuplot.write('''set format x \"\"\n''')
      gnuplot.write('''set format y \"%.2f s\"\n''')
      gnuplot.write('''plot ''')
      gnuplot.write('''\'%s\' using 1:9 with boxes lt 3''' % f_filename)
      gnuplot.write('''\n''')

      gnuplot.write('''set nokey\n''')
      gnuplot.write('''set xlabel\"Tests\"\n''')
      gnuplot.write('''set ylabel\"Speedup\"\n''')
      gnuplot.write('''unset ytics\n''')
      gnuplot.write('''set ytics autofreq\n''')
      gnuplot.write('''set size    1, 0.5\n''')
      gnuplot.write('''set origin  0, 0.0\n''')
      gnuplot.write('''set bmargin\n''')
      gnuplot.write('''set tmargin 0\n''')
      gnuplot.write('''set format x\n''')
      gnuplot.write('''set format y \"x %.2f\"\n''')
      gnuplot.write('''plot ''')
      gnuplot.write('''\'%s\' using 1:($8>1?1:1/0):($8>1?1:1/0):($8>1?$8:1/0):($8>1?$8:1/0) with candlesticks lt 2 lw 1 whiskerbars''' % f_filename)
      gnuplot.write(''',''')
      gnuplot.write('''\'%s\' using 1:($8>1?1/0:$8):($8>1?1/0:$8):($8>1?1/0:1):($8>1?1/0:1) with candlesticks lt 1 lw 1 whiskerbars''' % f_filename)
      gnuplot.write(''',''')
      gnuplot.write('''1 with lines ls 3''')
      gnuplot.write('''\n''')

      gnuplot.write('''exit\n''')
      gnuplot_process.wait()

      # Generate a BoxPlot
      gnuplot_process = subprocess.Popen(shlex.split(plot_command), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
      gnuplot         = gnuplot_process.stdin

      gnuplot.write('''set xlabel\"Tests\"\n''')
      gnuplot.write('''set ylabel\"Running time (seconds)\"\n''')
      gnuplot.write('''set xrange [%f:%f]\n''' % (mintestex-0.5, maxtestex+0.5))
      gnuplot.write('''set xtics 0, %d\n''' % (maxtestex - mintestex <= 100 and 10 or 100 ))
      gnuplot.write('''set grid x\n''')
      gnuplot.write('''set grid y\n''')
      gnuplot.write('''set style line 1 lw 1 lc rgb \"#23b323\"\n''')
      gnuplot.write('''set style line 2 lw 1 lc rgb \"#e41919\"\n''')
      gnuplot.write('''set style fill solid noborder\n''')
      gnuplot.write('''set boxwidth 0.95\n''')
      gnuplot.write('''set terminal png size 1024,256\n''')
      gnuplot.write('''set output \'%s\'\n''' % os.path.join(self._base_dir, 'plot_bp.png'))
      gnuplot.write('''plot ''')
      gnuplot.write('''\'%s\' using 1:3:2:4:4 with candlesticks ls 2 whiskerbars''' % f_filename)
      gnuplot.write(''',''')
      gnuplot.write('''\'%s\' using 1:4:4:6:5 with candlesticks ls 1 whiskerbars''' % f_filename)
      gnuplot.write('''\n''')

      gnuplot.write('''exit\n''')
      gnuplot_process.wait()

      path.remove(f_filename)
