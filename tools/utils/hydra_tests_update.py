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
import os
import argparse

parser = argparse.ArgumentParser(
                    prog='check_new_hydra_tests',
                    description='Compares the hydra test groups with the successful tests from the testsuite',)
parser.add_argument('-t', '--testsuite')
parser.add_argument('-g', '--groups')
parser.add_argument('-u', '--update')
args = parser.parse_args()

# Path to the testsuite results to scan
# -> "/path/to/arnold-usd/build/darwin_arm64-x86_64/gcc_debug/usd-0.23.11_arnold-7.3.0.0/testsuite"
TESTSUITE_DIR = args.testsuite

# Path to the groups file
# -> "/path/to/arnold-usd/testsuite/groups"
GROUPS_FILE = args.groups 

successful_tests = []
for subfolder in os.listdir(TESTSUITE_DIR):
    STATUS_FILE = os.path.join(TESTSUITE_DIR, subfolder, "STATUS")
    if os.path.exists(STATUS_FILE):
        with open(STATUS_FILE) as status_file_handle:
            if status_file_handle.read() == "OK":
                successful_tests.append(subfolder)

hydra_tests = []
with open(GROUPS_FILE, 'r') as f:
    for line in f.readlines():
        if line.startswith("hydra:"):
            hydra_tests = line[6:].split()

hydra_tests = set(hydra_tests)
successful_tests = set(successful_tests)
new_tests = successful_tests - hydra_tests
for test in new_tests:
    print(test)

# Update the groups file 
if args.update:
    new_list = list(hydra_tests) + list(new_tests)
    new_list.sort()
    lines = []
    with open(GROUPS_FILE, 'r') as f:
        for line in f.readlines():
            if line.startswith("hydra:"):
                lines.append("hydra: {}\n".format(" ".join(new_list)))
            else:
                lines.append(line)
    with open(GROUPS_FILE, 'w') as f:
        for line in lines:
            f.write(line)
