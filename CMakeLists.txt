project(arnold-usd)

cmake_minimum_required(VERSION 3.20)


# In Xcode and VisualC++ this will allow to organise the files following the folder structure
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif ()

if (LINUX)
    set(LIB_EXTENSION ".so")
    add_compile_definitions(_LINUX)
elseif (APPLE)
    set(LIB_EXTENSION ".dylib")
    add_compile_definitions(_DARWIN)
else ()
    set(LIB_EXTENSION ".dll")
    add_compile_definitions(_WINDOWS _WIN32 WIN32)
    add_compile_definitions(_WIN64)
    add_link_options(/DEBUG)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules" 0)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/utils" 0)


include(options)
include(build)

# Global required packagse
find_package(USD REQUIRED)
find_package(Arnold REQUIRED)

set(CMAKE_CXX_STANDARD 14 CACHE STRING "CMake CXX Standard")

if (APPLE)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,-undefined,error")
elseif (LINUX)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,--no-undefined")
endif ()

if (WIN32)
    # So we can use std::min and std::max, because windows headers are indirectly included by TF.
    add_compile_definitions(NOMINMAX)
    if (TBB_NO_EXPLICIT_LINKAGE)
        add_compile_definitions(__TBB_NO_IMPLICIT_LINKAGE=1)
    endif ()

    if (MSVC)
        if (MSVC_VERSION GREATER_EQUAL 1920)
            set(CMAKE_CXX_FLAGS "/Zc:inline- ${CMAKE_CXX_FLAGS}")
        endif ()
    endif ()
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 6.3)
        # GCC 6.3.1 complains about the usage of auto_ptr from the newer
        # TBB versions.
        add_compile_options(-Wno-deprecated-declarations)
        if (BUILD_DISABLE_CXX11_ABI)
            add_compile_options(-D_GLIBCXX_USE_CXX11_ABI=0)
        endif ()
    endif ()
endif ()

add_subdirectory(libs)

add_subdirectory(plugins)

if (BUILD_SCHEMAS AND USD_HAS_PYTHON)
    add_subdirectory(schemas)
    install(FILES plugInfo.json
        DESTINATION "${PREFIX_SCHEMA}")
endif ()


if (BUILD_DOCS)
    add_subdirectory(docs)
endif ()

if (BUILD_TESTSUITE)
    enable_testing()
    add_subdirectory(testsuite)
endif ()

if (NOT USD_STATIC_BUILD)
    if (BUILD_RENDER_DELEGATE OR BUILD_NDR_PLUGIN OR BUILD_USD_IMAGING_PLUGIN OR BUILD_SCENE_DELEGATE)
        install(FILES plugInfo.json
            DESTINATION "${PREFIX_PLUGINS}")
    endif ()
endif()

install(FILES LICENSE.md
    DESTINATION .)
