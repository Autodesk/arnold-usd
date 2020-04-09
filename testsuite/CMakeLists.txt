# Building unit tests.
if (BUILD_UNIT_TESTS)
    find_package(GoogleTest REQUIRED)
    include("${CMAKE_SOURCE_DIR}/cmake/utils/test.cmake")

    add_unit_test(
            GTEST
            TEST_NAME test_0039
            MAIN_DEPENDENCY hdArnold
    )

endif ()