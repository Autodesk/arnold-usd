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
endif ()
set(Boost_NO_BOOST_CMAKE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules" 0)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/utils" 0)

set(CMAKE_CXX_STANDARD 14)

include(options)
include(build)

# Global required packagse
find_package(USD REQUIRED)
find_package(Arnold REQUIRED)

if (BUILD_SCHEMAS OR (BUILD_TESTSUITE AND BUILD_RENDER_DELEGATE AND BUILD_NDR_PLUGIN))
    if (BUILD_USE_PYTHON3)
        find_package(Python3 COMPONENTS Development Interpreter REQUIRED)
    else ()
        find_package(Python2 COMPONENTS Development Interpreter REQUIRED)
    endif ()
else ()
    if (BUILD_USE_PYTHON3)
        find_package(Python3 COMPONENTS Development REQUIRED)
    else ()
        find_package(Python2 COMPONENTS Development REQUIRED)
    endif ()
endif ()

if (BUILD_USE_PYTHON3)
    set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
else ()
    set(PYTHON_EXECUTABLE ${Python2_EXECUTABLE})
endif ()

if (NOT BUILD_USE_CUSTOM_BOOST)
    if (USD_HAS_PYTHON)
        find_package(Boost COMPONENTS python REQUIRED)
    else ()
        find_package(Boost REQUIRED)
    endif ()
endif ()
# This disables explicit linking from boost headers on Windows.
if (BUILD_BOOST_ALL_NO_LIB AND WIN32)
    add_compile_definitions(BOOST_ALL_NO_LIB=1)
    # This is for Houdini's boost libs.
    add_compile_definitions(HBOOST_ALL_NO_LIB=1)
endif ()

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

if (ENABLE_MATERIALX)
    add_compile_options(-DARNOLD_MATERIALX=1)
endif ()

# Common directory includes.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/common")
set(COMMON_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/common/common_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/constant_strings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/shape_utils.cpp")

set(COMMON_HDR
    "${CMAKE_CURRENT_SOURCE_DIR}/common/common_bits.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/common_utils.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/constant_strings.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/shape_utils.h")

if (BUILD_SCHEMAS AND USD_HAS_PYTHON)
    add_subdirectory(schemas)
    install(FILES plugInfo.json
        DESTINATION "${PREFIX_SCHEMA}")
endif ()

if (NOT USD_STATIC_BUILD)
    if (BUILD_NDR_PLUGIN)
        add_subdirectory(ndr)
    endif ()

    if (BUILD_RENDER_DELEGATE)
        add_subdirectory(render_delegate)
    endif ()

    if (BUILD_USD_IMAGING_PLUGIN)
        add_subdirectory(usd_imaging)
    endif ()

    if (BUILD_SCENE_DELEGATE)
        add_subdirectory(scene_delegate)
    endif ()

    if (BUILD_RENDER_DELEGATE OR BUILD_NDR_PLUGIN OR BUILD_USD_IMAGING_PLUGIN OR BUILD_SCENE_DELEGATE)
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

#if (BUILD_TESTSUITE)
#    enable_testing()
#    add_subdirectory(testsuite)
#endif ()


install(FILES LICENSE.md
    DESTINATION .)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/arnold_usd.h"
    DESTINATION "${PREFIX_HEADERS}/arnold_usd")
