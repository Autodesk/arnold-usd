# Building unit tests.
include("${CMAKE_SOURCE_DIR}/cmake/utils/test.cmake")

ignore_test(test_0011 test_0040 test_0101 test_0108)
add_render_delegate_unit_test(test_0039 test_0134 test_0136 test_0146 test_0147 test_0152 test_0153)
add_ndr_unit_test(test_0044)
add_translator_unit_test(test_0045)

discover_render_tests()