# Copyright 2020 Autodesk, Inc.
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
# Utilities for running and building tests.

# GTEST if gtest needs to be linked to the library.
# GTEST_MAIN if gtest main needs to be linked to the library.
# GMOCK if gmock needs to be linked to the library.
# GMOCK_MAIN if gmock main needs to be linked to the library.
# TEST_NAME to set the test's name, which has to be the same as the subdirrectory name in the testsuite directory.
# MAIN_DEPENDENCY to set the module being tested, which can be translator, hdArnold or ndrArnold.
function(add_unit_test)
    set(add_unit_test_options GTEST GTEST_MAIN GMOCK GMOCK_MAIN)
    set(add_unit_test_one_value_args TEST_NAME MAIN_DEPENDENCY)
    set(add_unit_test_multi_value_args "")

    cmake_parse_arguments(add_unit_test "${add_unit_test_options}" "${add_unit_test_one_value_args}" "${add_unit_test_multi_value_args}" ${ARGN})

    add_executable(${add_unit_test_TEST_NAME} ${CMAKE_SOURCE_DIR}/testsuite/${add_unit_test_TEST_NAME}/data/test.cpp)
    if (${add_unit_test_GTEST} OR ${add_unit_test_GMOCK})
        target_include_directories(${add_unit_test_TEST_NAME} PUBLIC "${GTEST_INCLUDE_DIR}")
    endif ()
    target_include_directories(${add_unit_test_TEST_NAME} PUBLIC "${CMAKE_SOURCE_DIR}")

    if (LINUX)
        target_link_libraries(${add_unit_test_TEST_NAME} PUBLIC pthread)
    endif ()
    target_link_libraries(${add_unit_test_TEST_NAME} PUBLIC ${add_unit_test_MAIN_DEPENDENCY})
    if (${add_unit_test_GTEST})
        target_link_libraries(${add_unit_test_TEST_NAME} PUBLIC "${GTEST_LIBRARY}")
    endif ()
    if (${add_unit_test_GTEST_MAIN})
        target_link_libraries(${add_unit_test_TEST_NAME} PUBLIC "${GTEST_MAIN_LIBRARY}")
    endif ()
    if (${add_unit_test_GMOCK})
        target_link_libraries(${add_unit_test_TEST_NAME} PUBLIC "${GMOCK_LIBRARY}")
    endif ()
    if (${add_unit_test_GMOCK_MAIN})
        target_link_libraries(${add_unit_test_TEST_NAME} PUBLIC "${GMOCK_MAIN_LIBRARY}")
    endif ()

    add_test(NAME ${add_unit_test_TEST_NAME} COMMAND $<TARGET_FILE:${add_unit_test_TEST_NAME}>)
endfunction()
