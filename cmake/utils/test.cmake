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

    # Common unit tests don't link a library, they include the sources directly.
    # This is done, because libraries don't expose symbols from common utils.
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
    # TODO(pal): Investigate if these environment sets do work. After running the render tests, looks like
    #  they might not work. Maybe we could generate a batch script here as well, that configures the env etc?
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

# Add new unit test for the render delegate library.
function(add_render_delegate_unit_test)
    # We are ignoring the tests so the scripts that automatically scan the folders will skip these folders.
    ignore_test(${ARGN})
    if (NOT BUILD_RENDER_DELEGATE OR USD_STATIC_BUILD)
        return()
    endif ()
    foreach (_test_name ${ARGN})
        add_unit_test(GTEST
            TEST_NAME ${_test_name}
            MAIN_DEPENDENCY hdArnold)
    endforeach ()
endfunction()

# Add new unit test for the ndr library.
function(add_ndr_unit_test)
    # We are ignoring the tests so the scripts that automatically scan the folders will skip these folders.
    ignore_test(${ARGN})
    if (NOT BUILD_NDR_PLUGIN OR USD_STATIC_BUILD)
        return()
    endif ()
    foreach (_test_name ${ARGN})
        add_unit_test(GTEST
            TEST_NAME ${_test_name}
            MAIN_DEPENDENCY ndrArnold)
    endforeach ()
endfunction()

# Add new unit test for the translator library.
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
    set(_force_expand OFF)
    set(_scene "test.ass")
    set(_readme "${dir}/README")
    set(_kick_params "")
    # Readme should exists all the time.
    if (EXISTS "${_readme}")
        file(STRINGS
            "${_readme}"
            _tmp_match
            REGEX "PARAMS[ ]*\\:[ ]*{[^}]*}")
        if (_tmp_match MATCHES "'resaved'[ ]*\\:[ ]*'(ass|usda)'")
            set(_resaved "${CMAKE_MATCH_1}")
        endif ()
        if (_tmp_match MATCHES "'forceexpand'[ ]*\\:[ ]*True")
            set(_force_expand ON)
        endif ()
        if (_tmp_match MATCHES "'scene'[ ]*\\:[ ]*'([^']+)'")
            set(_scene "${CMAKE_MATCH_1}")
        endif ()
        if (_tmp_match MATCHES "'kick_params'[ ]*\\:[ ]*'([^']+)'")
            set(_kick_params "${CMAKE_MATCH_1}")
        endif ()
    endif ()

    # Determining the extension and the base file name.
    if (_scene MATCHES "([a-z]+)\.([a-z]+)")
        set(_scene_base "${CMAKE_MATCH_1}")
        set(_scene_extension "${CMAKE_MATCH_2}")
    else ()
        # We can't match a scene file name correct, exit.
        return ()
    endif ()

    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/${test_name}")
    make_directory("${_out_dir}")

    set(_input_scene "${dir}/data/${_scene}")
    set(_input_reference "${dir}/ref/reference.tif")
    set(_input_log "${dir}/ref/reference.log")
    # Only supporting render tests for now.
    if (NOT EXISTS "${_input_scene}")
        return()
    endif ()

    set(_input_file "${_out_dir}/${_scene}")
    set(_output_render "${_out_dir}/testrender.tif")
    set(_output_difference "${_out_dir}/diff.tif")
    set(_output_log "${_out_dir}/${test_name}.log")

    # Copying all the files from data to the output directory.
    file(GLOB _data_files "${dir}/data/*")
    file(COPY ${_data_files} DESTINATION "${_out_dir}")
    file(COPY "${dir}/README" DESTINATION "${_out_dir}")

    # Since we need to execute multiple commands, and add_test supports a single command, we are generating a test
    # command.

    # We can't have generator expressions in configure file, and the CONTENT field only takes a single variable, 
    # so we have to join with an newline string to convert a list to a single string that's correctly formatted.
    # We need to use ARNOLD_PLUGIN_PATH instead of -l to load the procedural, otherwise our build won't get picked up.
    if (WIN32)
        set(_cmd
            "del /f /q \"${_output_render}\""
            "del /f /q \"${_output_log}\""
            "del /f /q \"${_output_difference}\""
            "setx ARNOLD_PLUGIN_PATH \"$<TARGET_FILE_DIR:${USD_PROCEDURAL_NAME}_proc>\""
            )
    else ()
        set(_cmd
            "rm -f \"${_output_render}\""
            "rm -f \"${_output_log}\""
            "rm -f \"${_output_difference}\""
            "export ARNOLD_PLUGIN_PATH=\"$<TARGET_FILE_DIR:${USD_PROCEDURAL_NAME}_proc>\""
        )
    endif ()
    # We have to setup the pxr plugin path to point at the original schema files if we are creating a static build
    # of the procedural. This also could be overwritten in the USD build, and this information is not included in
    # the USD library headers.
    if (USD_STATIC_BUILD AND BUILD_PROCEDURAL)
        if (WIN32)
            list(APPEND _cmd "setx ${USD_OVERRIDE_PLUGINPATH_NAME} \"${USD_LIBRARY_DIR}/usd\"")
        else ()
            list(APPEND _cmd "export ${USD_OVERRIDE_PLUGINPATH_NAME}=\"${USD_LIBRARY_DIR}/usd\"")
        endif ()
    endif ()

    set(_render_file "${_input_file}")
    # If a test is resaved, we need to add a command that resaves the file and render that instead.
    if (_resaved MATCHES ".+")
        if (_forceexpand)
            set(_cmd_force_expand "-forceexpand")
        else ()
            set(_cmd_force_expand "")
        endif ()
        set(_render_file "${_out_dir}/${_scene_base}_resaved.${_resaved}")
        list(APPEND _cmd "\"${ARNOLD_KICK}\" -i \"${_input_file}\" -resave \"${_render_file}\" ${_cmd_force_expand}")
    endif ()
    # Shared args for rendering tests and generating reference files.
    set(_kick_args "-r ${TEST_RESOLUTION} -sm lambert -bs 16 -set driver_tiff.dither false -dp -dw -v 6 ${_kick_params}")
    # Rendering the file.
    list(APPEND _cmd "\"${ARNOLD_KICK}\" ${_kick_args} -i \"${_render_file}\" -o \"${_output_render}\" -logfile \"${_output_log}\"")
    # TODO(pal): Investigate how best read existing channel definitions from the file.
    if (TEST_MAKE_THUMBNAILS)
        # Creating thumbnails for reference and new image.
        list(APPEND _cmd "\"${ARNOLD_OIIOTOOL}\" \"${_output_render}\" --threads 1 --ch \"R,G,B\" -o ${_out_dir}/new.png")
        list(APPEND _cmd "\"${ARNOLD_OIIOTOOL}\" \"${_input_reference}\" --threads 1 --ch \"R,G,B\" -o ${_out_dir}/ref.png")
        # Adding diffing commands.
        set(_cmd_thumbnails "--sub --abs --cmul 8 -ch \"R,G,B,A\" --dup --ch \"A,A,A,0\" --add -ch \"0,1,2\" -o dif.png")
    else ()
        set(_cmd_thumbnails "")
    endif ()
    # Diffing the result to the reference file.
    list(APPEND _cmd "\"${ARNOLD_OIIOTOOL}\" --threads 1 --hardfail ${TEST_DIFF_HARDFAIL} --fail ${TEST_DIFF_FAIL} --failpercent ${TEST_DIFF_FAILPERCENT} --warnpercent ${TEST_DIFF_WARNPERCENT} --diff \"${_output_render}\" \"${_input_reference}\" ${_cmd_thumbnails}")
    if (WIN32)
        set(_cmd_ext ".bat")
    else ()
        set(_cmd_ext ".sh")
    endif ()

    # CMake has several generators that are support multiple configurations at once, like on Visual Sudio on windows.
    # This means we have to generate several command files for each configuration, using different procedurals, otherwise
    # we would get cmake warnings and potentially running the wrong binary for each test. We are still using the
    # same output directory for each config, as the expectation is to work with a single config at any given time.
    string(JOIN "\n" _cmd ${_cmd})
    file(
        GENERATE OUTPUT "${_out_dir}/test_$<CONFIG>${_cmd_ext}"
        CONTENT "${_cmd}"
        FILE_PERMISSIONS OWNER_EXECUTE OWNER_READ
        NEWLINE_STYLE UNIX
    )

    add_test(
        NAME ${test_name}
        COMMAND "${_out_dir}/test_$<CONFIG>${_cmd_ext}"
        WORKING_DIRECTORY "${_out_dir}"
    )

    if (WIN32)
        set(_cmd_generate "setx ARNOLD_PLUGIN_PATH \"$<TARGET_FILE_DIR:${USD_PROCEDURAL_NAME}_proc>\"")
    else ()
        set(_cmd_generate "export ARNOLD_PLUGIN_PATH=\"$<TARGET_FILE_DIR:${USD_PROCEDURAL_NAME}_proc>\"")
    endif ()
    if (USD_STATIC_BUILD AND BUILD_PROCEDURAL)
        if (WIN32)
            list(APPEND _cmd_generate "setx ${USD_OVERRIDE_PLUGINPATH_NAME} \"${USD_LIBRARY_DIR}/usd\"")
        else ()
            list(APPEND _cmd_generate "export ${USD_OVERRIDE_PLUGINPATH_NAME}=\"${USD_LIBRARY_DIR}/usd\"")
        endif ()
    endif ()
    set(_generate_kick "\"${ARNOLD_KICK}\" ${_kick_args} -i \"${_input_scene}\" -o \"${_input_reference}\" -logfile \"${_input_log}\"")
    if (WIN32)
        list(APPEND _cmd_generate "if not exists \"${_input_reference}\" ${_generate_kick}")
    else ()
        list(APPEND _cmd_generate "[[ -f \"${_input_reference}\" ]] || ${_generate_kick}")
    endif ()
    string(JOIN "\n" _cmd_generate ${_cmd_generate})
    file(
        GENERATE OUTPUT "${_out_dir}/test_generate_$<CONFIG>${_cmd_ext}"
        CONTENT "${_cmd_generate}"
        FILE_PERMISSIONS OWNER_EXECUTE OWNER_READ
        NEWLINE_STYLE UNIX
    )

    # BYPRODUCTS is not really useful for us, as commands like make clean will remove any byproducts.
    # We add a custom target that depends on the procedural, so we can render the reference image and output reference log.
    add_custom_target(
        ${test_name}_generate
        "${_out_dir}/test_generate_$<CONFIG>${_cmd_ext}"
        DEPENDS ${USD_PROCEDURAL_NAME}_proc
        SOURCES ${_data_files}
        WORKING_DIRECTORY "${dir}/data"
    )
endfunction()

function(discover_render_tests)
    # We skip render tests if the procedural is not built for now. In the future we should be able to use the render
    # delegate to render without 
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
