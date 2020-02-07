project(arnold-usd)

cmake_minimum_required(VERSION 3.12)

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
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules" 0)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/utils" 0)

include(options)

# Global required packagse
find_package(USD REQUIRED)
find_package(Arnold REQUIRED)

if (NOT BUILD_USE_CUSTOM_BOOST)
    if (USD_HAS_PYTHON)
        find_package(Boost COMPONENTS python REQUIRED)
    else ()
        find_package(Boost REQUIRED)
    endif ()
endif ()
find_package(Python2 COMPONENTS Development Interpreter REQUIRED)
find_package(TBB REQUIRED)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/arnold_usd.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/arnold_usd.h")

set(CMAKE_CXX_STANDARD 11 CACHE STRING "CMake CXX Standard")

if (APPLE)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,-undefined,error")
elseif (LINUX)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,--no-undefined")
endif ()

if (WIN32)
    # So we can use std::min and std::max, because windows headers are indirectly included by TF.
    add_compile_definitions(NOMINMAX)
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

if (NOT USD_STATIC_BUILD)
    if (BUILD_SCHEMAS AND USD_HAS_PYTHON)
        add_subdirectory(schemas)
        install(FILES plugInfo.json
                DESTINATION "${PREFIX_LIB}/usd")
    endif ()

    if (BUILD_NDR_PLUGIN)
        add_subdirectory(ndr)
    endif ()

    if (BUILD_RENDER_DELEGATE)
        add_subdirectory(render_delegate)
    endif()

    if (BUILD_RENDER_DELEGATE OR BUILD_NDR_PLUGIN)
        install(FILES plugInfo.json
                DESTINATION "${PREFIX_PLUGINS}")
    endif ()
endif ()

if (BUILD_PROCEDURAL OR BUILD_USD_WRITER)
    add_subdirectory(translator)
endif ()

if (BUILD_PROCEDURAL)
    add_subdirectory(procedural)
endif ()

if (BUILD_USD_WRITER)
    add_subdirectory(cmd)
endif ()

if (BUILD_DOCS)
    add_subdirectory(docs)
endif ()

install(FILES LICENSE.md
        DESTINATION .)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/arnold_usd.h"
        DESTINATION "${PREFIX_HEADERS}")