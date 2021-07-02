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

if (BUILD_UNIT_TESTS)
    find_package(GoogleTest REQUIRED)
endif ()

set(_TEST_LIBRARY_PATHS "")
set_property(GLOBAL PROPERTY _IGNORED_TESTS "")

if (USD_LIBRARY_DIR)
    list(APPEND _TEST_LIBRARY_PATHS "${USD_LIBRARY_DIR}")
endif ()

if (USD_BINARY_DIR)
    list(APPEND _TEST_LIBRARY_PATHS "${USD_BINARY_DIR}")
endif ()

if (ARNOLD_BINARY_DIR)
    list(APPEND _TEST_LIBRARY_PATHS "${ARNOLD_BINARY_DIR}")
endif ()

foreach (_each "${Boost_LIBRARIES}")
    get_filename_component(_comp "${_each}" DIRECTORY)
    list(APPEND _TEST_LIBRARY_PATHS "${_each}")
endforeach ()

foreach (_each "${TBB_LIBRARIES}")
    get_filename_component(_comp "${_each}" DIRECTORY)
    list(APPEND _TEST_LIBRARY_PATHS "${_each}")
endforeach ()

foreach (_each "${Python2_LIBRARY_DIRS}")
    list(APPEND _TEST_LIBRARY_PATHS "${_each}")
endforeach ()

# Since the build scripts allow flexibility for linking Boost, TBB and Python
# we are going to iterate through all the possible folders stored there
# and add them the path.

if (WIN32)
    string(JOIN "\;" TEST_LIBRARY_PATHS ${_TEST_LIBRARY_PATHS})
elseif (LINUX)
    string(JOIN ":" TEST_LIBRARY_PATHS ${_TEST_LIBRARY_PATHS})
    set(TEST_LIBRARY_PATHS "${TEST_LIBRARY_PATHS}:$ENV{LD_LIBRARY_PATH}")
else ()
    string(JOIN ":" TEST_LIBRARY_PATHS ${_TEST_LIBRARY_PATHS})
    set(TEST_LIBRARY_PATHS "${TEST_LIBRARY_PATHS}:$ENV{DYLD_LIBRARY_PATH}")
endif ()

function(ignore_test)
    foreach (_test_name ${ARGN})
        get_property(_tmp GLOBAL PROPERTY _IGNORED_TESTS)
        # List append does not work with parent variables and this function might be called in other functions.
        set_property(GLOBAL PROPERTY _IGNORED_TESTS ${_tmp};${_test_name})
    endforeach ()
endfunction()

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
    # We are ignoring the tests so the scripts that automatically scan the folders will skip these folders.
    ignore_test(${ARGN})
    if (NOT BUILD_RENDER_DELEGATE)
        return()
    endif ()
    foreach (_test_name ${ARGN})
        add_unit_test(GTEST
            TEST_NAME ${_test_name}
            MAIN_DEPENDENCY hdArnold)
    endforeach ()
endfunction()

function(add_ndr_unit_test)
    # We are ignoring the tests so the scripts that automatically scan the folders will skip these folders.
    ignore_test(${ARGN})
    if (NOT BUILD_NDR_PLUGIN)
        return()
    endif ()
    foreach (_test_name ${ARGN})
        add_unit_test(GTEST
            TEST_NAME ${_test_name}
            MAIN_DEPENDENCY ndrArnold)
    endforeach ()
endfunction()

function(add_translator_unit_test)
    # We are ignoring the tests so the scripts that automatically scan the folders will skip these folders.
    ignore_test(${ARGN})
    if (NOT BUILD_PROCEDURAL AND NOT BUILD_USD_WRITER)
        return()
    endif ()
    foreach (_test_name ${ARGN})
        add_unit_test(GTEST
            TEST_NAME ${_test_name}
            MAIN_DEPENDENCY translator)
    endforeach ()
endfunction()

function(discover_render_test test_name dir)
    # First we check if there is a README file and parse the list of parameters
    set(_resaved "")
    set(_forceexpand OFF)
    set(_scene "test.ass")
    set(_readme "${dir}/README")
    # Readme should exists all the time.
    if (EXISTS "${_readme}")
        file(STRINGS
            "${_readme}"
            _tmp_match
            REGEX "PARAMS[ ]*\\:[ ]*{[^}]*}")
        if (_tmp_match MATCHES "'resaved'[ ]*\\:[ ]*'(ass|usda)'")
            set(_resaved ${CMAKE_MATCH_1})
        endif ()
        if (_tmp_match MATCHES "'forceexpand'[ ]*\\:[ ]*True")
            set(_forceexpand ON)
        endif ()
        if (_tmp_match MATCHES "'scene'[ ]*\\:[ ]*'([^']+)'")
            set(_scene "${CMAKE_MATCH_1}")
        endif ()
    endif ()

    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/${test_name}")
    make_directory("${_out_dir}")
    set(_test_cmd "${_out_dir}/test.sh")
    set(_scene_file "${dir}/data/${_scene}")
    set(_reference_render "${dir}/ref/reference.tif")
    # Only supporting render tests for now.
    if (NOT EXISTS "${_reference_render}")
        return()
    endif ()
    if (NOT EXISTS "${_scene_file}")
        return()
    endif ()
    set(_input_file "${_out_dir}/${_scene}")
    set(_output_render "${_out_dir}/testrender.tif")
    set(_output_difference "${_out_dir}/diff.tif")
    set(_output_log "${_out_dir}/test.log")

    # Copying all the files from data to the output directory.
    file(GLOB _data_files "${dir}/data/*")
    file(COPY ${_data_files} DESTINATION "${_out_dir}")

    # Since we need to execute multiple commands, and add_test supports a single command, we are generating a test
    # command.

    # We can't have generator expressions in configure file, and the CONTENT field only takes a single variable, 
    # so we have to join with an newline string to convert a list to a single string that's correctly formatted.
    # We need to use ARNOLD_PLUGIN_PATH instead of -l to load the procedural, otherwise our build won't get picked up.
    set(_content
        "rm -f \"${_output_render}\""
        "rm -f \"${_output_log}\""
        "rm -f \"${_output_difference}\""
        "export ARNOLD_PLUGIN_PATH=\"$<TARGET_FILE_DIR:${USD_PROCEDURAL_NAME}_proc>\""
        "\"${ARNOLD_KICK}\" -dp -dw -i \"${_scene_file}\" -o \"${_output_render}\" -v 5 -logfile \"${_output_log}\""
    )
    string(JOIN "\n" _content ${_content})

    file(
        GENERATE OUTPUT "${_test_cmd}"
        CONTENT "${_content}"
        FILE_PERMISSIONS OWNER_EXECUTE OWNER_READ
        NEWLINE_STYLE UNIX
    )

    add_test(
        NAME ${test_name}
        COMMAND "${_test_cmd}"
        WORKING_DIRECTORY "${_out_dir}"
    )

    # We add a custom target that depends on the procedural, so we can render the reference image and reference log.
endfunction()

function(discover_render_tests)
    # We skip render tests if the procedural is not built.
    if (NOT BUILD_PROCEDURAL)
        return()
    endif ()
    file(GLOB _subs RELATIVE "${CMAKE_SOURCE_DIR}/testsuite" "${CMAKE_SOURCE_DIR}/testsuite/*")
    get_property(_ignored_tests GLOBAL PROPERTY _IGNORED_TESTS)
    foreach (_iter ${_subs})
        if (_iter STREQUAL "common" OR _iter IN_LIST _ignored_tests)
            continue()
        endif ()
        set(_sub "${CMAKE_SOURCE_DIR}/testsuite/${_iter}")
        if (IS_DIRECTORY "${_sub}")
            discover_render_test("${_iter}" "${_sub}")
        endif ()
    endforeach ()
endfunction()
