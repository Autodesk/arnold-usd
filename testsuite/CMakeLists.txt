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
# Using abuild to run the testsuite
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/testsuite"
    COMMAND
	"ARNOLD_PLUGIN_PATH=$<TARGET_FILE_DIR:usd_proc>" ./abuild USD_PROCEDURAL_PATH=$<TARGET_FILE_DIR:usd_proc> USD_PATH=${USD_LOCATION} BOOST_INCLUDE=${USD_LOCATION}/include ARNOLD_PATH=${ARNOLD_LOCATION} BUILD_SCHEMAS=0 BUILD_DOCS=0 BUILD_TESTSUITE=1 testsuite
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_custom_target(testsuite
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/testsuite"
)
add_dependencies(testsuite usd_proc)


