# Building unit tests with cmake and ctest
#include("${CMAKE_SOURCE_DIR}/cmake/utils/test.cmake")
#
#ignore_test(test_0011 test_0040 test_0101 test_0108)
##add_render_delegate_unit_test(test_0039 test_0134 test_0146 test_0147 test_0152 test_0153 test_0154 test_0155 test_0156)
##add_ndr_unit_test(test_0044)
## test 136 requires only common utils, but we use the translator to automatically include headers, link libraries etc.
##add_translator_unit_test(test_0045 test_0136)
#
## Copy the common folder into the build folder
#file(COPY common DESTINATION ${CMAKE_BINARY_DIR})
#
#discover_render_tests()
#${CMAKE_COMMAND} -E env
#
# Now using abuild to run the testsuite

# If the bundle is built it will be used instead of the procedural
set(PROC_TEST_FOLDER procedural)
if (BUILD_BUNDLE)
    set(PROC_TEST_FOLDER bundle)
endif()


# What abuild needs to know about this build, rather than reading it from a custom.py
if (BUILD_WITH_USD_STATIC)
    set(TESTSUITE_USD_BUILD_MODE static)
elseif (USD_MONOLITHIC_BUILD)
    set(TESTSUITE_USD_BUILD_MODE monolithic)
else ()
    set(TESTSUITE_USD_BUILD_MODE shared_libs)
endif ()
set(TESTSUITE_ABUILD_ARGS USD_BUILD_MODE=${TESTSUITE_USD_BUILD_MODE})
if (APPLE)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _osx_arch_count)
    if (_osx_arch_count GREATER 1)
        set(_macos_arch universal)
    elseif (CMAKE_OSX_ARCHITECTURES)
        set(_macos_arch ${CMAKE_OSX_ARCHITECTURES})
    else ()
        set(_macos_arch ${CMAKE_HOST_SYSTEM_PROCESSOR})
    endif ()
    list(APPEND TESTSUITE_ABUILD_ARGS MACOS_ARCH=${_macos_arch})
endif ()

# The python interactive render delegate tests (testsuite/common/interactive_engine_test.py) run with the interpreter
# of USD, and read the AOVs back with the module of libs/test_support
set(TESTSUITE_ENV)
if (Python3_EXECUTABLE)
    list(APPEND TESTSUITE_ABUILD_ARGS "TESTSUITE_PYTHON=$<SHELL_PATH:${Python3_EXECUTABLE}>")
endif ()
if (TARGET interactive_engine_readback)
    list(APPEND TESTSUITE_ENV
        "INTERACTIVE_ENGINE_READBACK_PATH=$<SHELL_PATH:${CMAKE_INSTALL_PREFIX}/${PREFIX_TESTSUITE_PYTHON}>")
endif ()

# Only the procedural reads the schemas from its own folder
set(TESTSUITE_COPY_SCHEMAS)
if (BUILD_PROCEDURAL)
    # TODO: the install should do that ? shouldn't it ?
    set(TESTSUITE_COPY_SCHEMAS COMMAND
        ${CMAKE_COMMAND} -E copy_directory "${CMAKE_INSTALL_PREFIX}/schema/usdArnold" "${CMAKE_INSTALL_PREFIX}/procedural/usd")
endif ()

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/testsuite"
    # First we install our cmake build as we want to test the install. We use the scons build folder as it should be there.
    COMMAND 
        ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix="${CMAKE_INSTALL_PREFIX}"
    ${TESTSUITE_COPY_SCHEMAS}
    # Run the scons testsuite pointing to our install, with the tests of TESTSUITE_PATTERNS
    #       change the environment variable depending on the usd lib we build with (shared would need <install>/plugin in PATH)
    COMMAND
        ${CMAKE_COMMAND} -E env
            "USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX=1"
            "TESTSUITE_ARNOLD_PLUGIN_PATH=$<SHELL_PATH:${CMAKE_INSTALL_PREFIX}/${PROC_TEST_FOLDER}>"
            "TESTSUITE_PXR_PLUGINPATH_NAME=$<SHELL_PATH:${CMAKE_INSTALL_PREFIX}/schema;${CMAKE_INSTALL_PREFIX}/${PROC_TEST_FOLDER}>"
            "DYLD_LIBRARY_PATH=$<SHELL_PATH:${USD_LOCATION}/lib;${USD_LOCATION}/bin;${ARNOLD_LOCATION}/bin;${CMAKE_INSTALL_PREFIX}/${PROC_TEST_FOLDER};$ENV{DYLD_LIBRARY_PATH}>"
            "PYTHONPATH=$<SHELL_PATH:${ARNOLD_PYTHON_DIR};${USD_LOCATION}/lib/python>"
            "LD_LIBRARY_PATH=$<SHELL_PATH:${USD_LOCATION}/lib;${USD_LOCATION}/bin;${ARNOLD_LOCATION}/bin;${CMAKE_INSTALL_PREFIX}/${PROC_TEST_FOLDER};$ENV{LD_LIBRARY_PATH}>"
            "PATH=$<SHELL_PATH:${USD_LOCATION}/lib;${USD_LOCATION}/bin;${ARNOLD_LOCATION}/bin;${CMAKE_INSTALL_PREFIX}/${PROC_TEST_FOLDER};$ENV{PATH}>"
            ${TESTSUITE_ENV}
            $<IF:$<PLATFORM_ID:Windows>,abuild.bat,./abuild> USD_PATH="$<SHELL_PATH:${USD_LOCATION}>" BOOST_INCLUDE="$<SHELL_PATH:${USD_LOCATION}/include>" ARNOLD_PATH="$<SHELL_PATH:${ARNOLD_LOCATION}>" ${TESTSUITE_ABUILD_ARGS} BUILD_NDR_PLUGIN=0 BUILD_USD_IMAGING_PLUGIN=0 BUILD_RENDER_DELEGATE=0 BUILD_SCENE_INDEX_PLUGIN=1 BUILD_USD_WRITER=0 BUILD_PROCEDURAL=0 BUILD_SCHEMAS=0 BUILD_DOCS=0 BUILD_TESTSUITE=1 ${TESTSUITE_PATTERNS}

    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
add_custom_target(testsuite
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/testsuite"
)
foreach (_tested_target usd_proc ArnoldUsdBundle interactive_engine_readback)
    if (TARGET ${_tested_target})
        add_dependencies(testsuite ${_tested_target})
    endif ()
endforeach ()
