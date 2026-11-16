# Common directory includes.
add_subdirectory(common)
add_subdirectory(translator)
add_subdirectory(render_delegate)
if (BUILD_TESTSUITE_READBACK)
    add_subdirectory(test_support)
endif ()
