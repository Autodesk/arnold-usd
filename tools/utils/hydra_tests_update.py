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

def update_groups_file(testsuite_dir, groups_file):
    print("Updating groups file")
    print("Reading test results", testsuite_dir)
    successful_tests = []
    for subfolder in os.listdir(testsuite_dir):
        STATUS_FILE = os.path.join(testsuite_dir, subfolder, "STATUS")
        if os.path.exists(STATUS_FILE):
            with open(STATUS_FILE) as status_file_handle:
                if status_file_handle.read() == "OK":
                    successful_tests.append(subfolder)
    print("Reading groups file", groups_file)
    hydra_tests = []
    with open(groups_file, 'r') as f:
        for line in f.readlines():
            if line.startswith("hydra:"):
                hydra_tests = line[6:].split()

    hydra_tests = set(hydra_tests)
    successful_tests = set(successful_tests)
    new_tests = successful_tests - hydra_tests

    # Update the groups file 
    new_list = list(hydra_tests) + list(new_tests)
    new_list.sort(key=lambda x:float(x[5:]))
    lines = []
    print("Writing group file")
    with open(groups_file, 'r') as f:
        for line in f.readlines():
            if line.startswith("hydra:"):
                lines.append("hydra: {}\n".format(" ".join(new_list)))
            else:
                lines.append(line)
    with open(groups_file, 'w') as f:
        for line in lines:
            f.write(line)
