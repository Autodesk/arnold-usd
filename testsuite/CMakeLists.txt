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

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/testsuite"
    # First we install our cmake build as we want to test the install. We use the scons build folder as it should be there.
    COMMAND 
        ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix="${CMAKE_INSTALL_PREFIX}"
    # We copy the schemas in the procedural 
    # TODO: the install should do that ? shouldn't it ?
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory "${CMAKE_INSTALL_PREFIX}/schema/usdArnold" "${CMAKE_INSTALL_PREFIX}/procedural/usd"
    # Run the scons testsuite pointing to our install. 
    # TODO: make it multiplatform to launch on windows as well
    #       add a way to run a set of tests instead of the whole testsuite
    COMMAND
        ${CMAKE_COMMAND} -E env
            "TESTSUITE_ARNOLD_PLUGIN_PATH=$<SHELL_PATH:${CMAKE_INSTALL_PREFIX}/procedural>"
            "TESTSUITE_PXR_PLUGINPATH_NAME=$<SHELL_PATH:${CMAKE_INSTALL_PREFIX}/schema;${CMAKE_INSTALL_PREFIX}/plugin>"
            "DYLD_LIBRARY_PATH=$<SHELL_PATH:${USD_LOCATION}/lib;${USD_LOCATION}/bin;${ARNOLD_LOCATION}/bin;$ENV{DYLD_LIBRARY_PATH}>"
            "PYTHONPATH=$<SHELL_PATH:${ARNOLD_PYTHON_DIR};${USD_LOCATION}/lib/python>"
            "LD_LIBRARY_PATH=$<SHELL_PATH:${USD_LOCATION}/lib;${USD_LOCATION}/bin;${ARNOLD_LOCATION}/bin;$ENV{LD_LIBRARY_PATH}>"
            "PATH=$<SHELL_PATH:${USD_LOCATION}/lib;${USD_LOCATION}/bin;${ARNOLD_LOCATION}/bin;$ENV{PATH}>"
            $<IF:$<PLATFORM_ID:Windows>,abuild.bat,./abuild> USD_PATH="$<SHELL_PATH:${USD_LOCATION}>" BOOST_INCLUDE="$<SHELL_PATH:${USD_LOCATION}/include>" ARNOLD_PATH="$<SHELL_PATH:${ARNOLD_LOCATION}>" BUILD_SCHEMAS=0 BUILD_DOCS=0 BUILD_TESTSUITE=1 testsuite

    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
add_custom_target(testsuite
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/testsuite"
)
add_dependencies(testsuite usd_proc)


