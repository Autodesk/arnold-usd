project(arnold-usd)

cmake_minimum_required(VERSION 3.1)

if (UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" 0)

option(BUILD_DOCS "Building docs via doxygen." OFF)
option(PXR_MONOLITHIC_BUILD "Monolithic build was used for USD." OFF)
option(BUILD_USE_CUSTOM_BOOST "Using a custom boost layout." OFF)
option(BUILD_DISABLE_CXX11_ABI "Disable the use of the new CXX11 ABI" OFF)
option(USD_1910_UPDATED_COMPOSITOR "USD 0.19.10 has the updated compositor" OFF)

# Global required packagse
find_package(OpenGL REQUIRED)
find_package(USD REQUIRED)
find_package(Arnold REQUIRED)
if (NOT BUILD_USE_CUSTOM_BOOST)
    find_package(Boost COMPONENTS python REQUIRED)
endif ()
find_package(PythonLibs REQUIRED)
find_package(TBB REQUIRED)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/arnold_usd.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/arnold_usd.h")

set(CMAKE_CXX_STANDARD 11 CACHE STRING "CMake CXX Standard")

if (LINUX)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,--no-undefined")
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

add_subdirectory(ndr)
add_subdirectory(render_delegate)

if (BUILD_DOCS)
    add_subdirectory(docs)
endif ()

install(FILES LICENSE.md
        DESTINATION .)

install(FILES plugInfo.json
        DESTINATION plugin)
