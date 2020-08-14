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

# Running the tests requires setting up environment variable to load dynamic libraries
# which is PATH on windows and LD_LIBRARY_PATH on Linux.
# We are not checking if a library is dynamically or statically linked, we
# just add all the paths that makes sense to the PATH/LD_LIBRARY_PATH and
# cache the full path globally.

set(TEST_LIBRARY_PATH_LIST "")

if (USD_LIBRARY_DIR)
    list(APPEND TEST_LIBRARY_PATH_LIST "${USD_LIBRARY_DIR}")
endif ()

if (USD_BINARY_DIR)
    list(APPEND TEST_LIBRARY_PATH_LIST "${USD_BINARY_DIR}")
endif ()

if (ARNOLD_BINARY_DIR)
    list(APPEND TEST_LIBRARY_PATH_LIST "${ARNOLD_BINARY_DIR}")
endif ()

foreach (_each "${Boost_LIBRARIES}")
    get_filename_component(_comp "${_each}" DIRECTORY)
    list(APPEND TEST_LIBRARY_PATH_LIST "${_each}")
endforeach ()

foreach (_each "${TBB_LIBRARIES}")
    get_filename_component(_comp "${_each}" DIRECTORY)
    list(APPEND TEST_LIBRARY_PATH_LIST "${_each}")
endforeach ()

foreach (_each "${Python2_LIBRARY_DIRS}")
    list(APPEND TEST_LIBRARY_PATH_LIST "${_each}")
endforeach ()

# Since the build scripts allow flexibility for linking Boost, TBB and Python
# we are going to iterate through all the possible folders stored there
# and add them the path.

if (WIN32)
    string(JOIN "\;" TEST_LIBRARY_PATHS ${TEST_LIBRARY_PATH_LIST})
elseif (LINUX)
    string(JOIN ":" TEST_LIBRARY_PATHS ${TEST_LIBRARY_PATH_LIST})
    set(TEST_LIBRARY_PATHS "${TEST_LIBRARY_PATHS}:$ENV{LD_LIBRARY_PATH}")
else ()
    string(JOIN ":" TEST_LIBRARY_PATHS ${TEST_LIBRARY_PATH_LIST})
    set(TEST_LIBRARY_PATHS "${TEST_LIBRARY_PATHS}:$ENV{DYLD_LIBRARY_PATH}")
endif ()

# GTEST if gtest needs to be linked to the library.
# GTEST_MAIN if gtest main needs to be linked to the library.
# GMOCK if gmock needs to be linked to the library.
# GMOCK_MAIN if gmock main needs to be linked to the library.
# TEST_NAME to set the test's name, which has to be the same as the subdirrectory name in the testsuite directory.
# MAIN_DEPENDENCY to set the module being tested, which can be translator, hdArnold or ndrArnold.
function(add_unit_test)
    if (NOT BUILD_UNIT_TESTS)
        return()
    endif ()
    set(_options GTEST GTEST_MAIN GMOCK GMOCK_MAIN)
    set(_one_value_args TEST_NAME MAIN_DEPENDENCY)
    set(_multi_value_args "")

    cmake_parse_arguments(_args "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    add_executable(${_args_TEST_NAME} "${CMAKE_SOURCE_DIR}/testsuite/${_args_TEST_NAME}/data/test.cpp")
    if (${_args_GTEST} OR ${_args_GMOCK})
        target_include_directories(${_args_TEST_NAME} PUBLIC "${GTEST_INCLUDE_DIR}")
    endif ()
    target_include_directories(${_args_TEST_NAME} PUBLIC "${CMAKE_SOURCE_DIR}")

    if (LINUX)
        target_link_libraries(${_args_TEST_NAME} PUBLIC pthread)
    endif ()
    target_link_libraries(${_args_TEST_NAME} PUBLIC ${_args_MAIN_DEPENDENCY})
    if (${_args_GTEST})
        target_link_libraries(${_args_TEST_NAME} PUBLIC "${GTEST_LIBRARY}")
    endif ()
    if (${_args_GTEST_MAIN})
        target_link_libraries(${_args_TEST_NAME} PUBLIC "${GTEST_MAIN_LIBRARY}")
    endif ()
    if (${_args_GMOCK})
        target_link_libraries(${_args_TEST_NAME} PUBLIC "${GMOCK_LIBRARY}")
    endif ()
    if (${_args_GMOCK_MAIN})
        target_link_libraries(${_args_TEST_NAME} PUBLIC "${GMOCK_MAIN_LIBRARY}")
    endif ()

    add_test(NAME ${_args_TEST_NAME} COMMAND $<TARGET_FILE:${_args_TEST_NAME}>)
    if (WIN32)
        set_tests_properties(${_args_TEST_NAME} PROPERTIES
            ENVIRONMENT "PATH=${TEST_LIBRARY_PATHS}\;$<TARGET_FILE_DIR:${_args_MAIN_DEPENDENCY}>")
    elseif (LINUX)
        set_tests_properties(${_args_TEST_NAME} PROPERTIES
            ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${_args_MAIN_DEPENDENCY}>:${TEST_LIBRARY_PATHS}")
    else ()
        set_tests_properties(${_args_TEST_NAME} PROPERTIES
            ENVIRONMENT "DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:${_args_MAIN_DEPENDENCY}>:${TEST_LIBRARY_PATHS}")
    endif ()
endfunction()

function(add_render_delegate_unit_test)
    if (NOT BUILD_RENDER_DELEGATE)
        return()
    endif ()
    set(_options "")
    set(_value_args "")
    set(_multi_value_args NAMES)

    cmake_parse_arguments(_args "${_options}" "${_value_args}" "${_multi_value_args}" ${ARGN})

    foreach(_test_name ${_args_NAMES})
        add_unit_test(GTEST
            TEST_NAME ${_test_name}
            MAIN_DEPENDENCY hdArnold)
    endforeach()
endfunction()

function(add_ndr_unit_test)
    if (NOT BUILD_NDR_PLUGIN)
        return()
    endif ()
    set(_options "")
    set(_value_args "")
    set(_multi_value_args NAMES)

    cmake_parse_arguments(_args "${_options}" "${_value_args}" "${_multi_value_args}" ${ARGN})

    foreach(_test_name ${_args_NAMES})
        add_unit_test(GTEST
            TEST_NAME ${_test_name}
            MAIN_DEPENDENCY ndrArnold)
    endforeach()
endfunction()

function(add_translator_unit_test)
    if (NOT BUILD_PROCEDURAL AND NOT BUILD_USD_WRITER)
        return()
    endif ()
    set(_options "")
    set(_value_args "")
    set(_multi_value_args NAMES)

    cmake_parse_arguments(_args "${_options}" "${_value_args}" "${_multi_value_args}" ${ARGN})

    foreach(_test_name ${_args_NAMES})
        add_unit_test(GTEST
            TEST_NAME ${_test_name}
            MAIN_DEPENDENCY translator)
    endforeach()
endfunction()
