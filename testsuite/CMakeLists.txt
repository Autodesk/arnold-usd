# Building unit tests.
include("${CMAKE_SOURCE_DIR}/cmake/utils/test.cmake")

ignore_test(test_0011 test_0040 test_0101 test_0108)
add_render_delegate_unit_test(test_0039 test_0134 test_0146 test_0147 test_0152 test_0153 test_0154 test_0155 test_0156)
add_ndr_unit_test(test_0044)
# test 136 requires only common utils, but we use the translator to automatically include headers, link libraries etc.
add_translator_unit_test(test_0045 test_0136)

discover_render_tests()