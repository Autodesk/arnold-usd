# Building unit tests.
if (BUILD_UNIT_TESTS)
    find_package(GoogleTest REQUIRED)
endif ()

include("${CMAKE_SOURCE_DIR}/cmake/utils/test.cmake")

add_render_delegate_unit_test(NAMES test_0039 test_0134 test_0136)
add_ndr_unit_test(NAMES test_0044)
add_translator_unit_test(NAMES test_0045)