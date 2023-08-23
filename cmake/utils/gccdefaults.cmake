if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 6.3)
    # GCC 6.3.1 complains about the usage of auto_ptr from the newer
    # TBB versions.
    add_compile_options(-Wno-deprecated-declarations)
    if (BUILD_DISABLE_CXX11_ABI)
        add_compile_options(-D_GLIBCXX_USE_CXX11_ABI=0)
    endif ()
endif ()