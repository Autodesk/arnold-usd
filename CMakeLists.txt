cmake_minimum_required(VERSION 3.24)

project(arnold-usd)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules" 0)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/utils" 0)
list(APPEND CMAKE_MODULE_PATH "${USD_LOCATION}/lib/cmake/" 0)

# Define the project and build options
include(options)

# Global required packagse
find_package(USD REQUIRED)
find_package(Arnold REQUIRED)

# Include build helper functions
include(build)

# Include default compiler options 
include(defaults)

# If we are building the bundle, we don't want to build the other plugins
if (BUILD_BUNDLE AND NOT BUILD_WITH_USD_STATIC)
    message(STATUS "Building Bundle - deactivating other plugins")
    set(BUILD_RENDER_DELEGATE OFF)
    message(STATUS "BUILD_RENDER_DELEGATE ${BUILD_RENDER_DELEGATE}")
    set(BUILD_NDR_PLUGIN OFF)
    set(BUILD_USD_IMAGING_PLUGIN OFF)
    set(BUILD_SCENE_INDEX_PLUGIN OFF)
    set(BUILD_PROCEDURAL OFF)
else()
    # And if we build with USD static, we don't want the bundle (for now)
    message(STATUS "Setting BUILD_BUNDLE=OFF and ENABLE_SCENE_INDEX_IN_BUNDLE=OFF")
    set(BUILD_BUNDLE OFF)
    set(ENABLE_SCENE_INDEX_IN_BUNDLE OFF)
endif()

# Adding libs and plugins
add_subdirectory(libs)
add_subdirectory(plugins)


if (BUILD_USDGENSCHEMA_ARNOLD)
    message(STATUS "Building custom usdGenSchema")
    add_subdirectory(tools/usdgenschema)
endif()

# Building the schemas
if (BUILD_SCHEMAS)
    add_subdirectory(schemas)
    install(FILES plugInfo.json
        DESTINATION "${PREFIX_SCHEMA}")
endif ()

if (BUILD_DOCS)
    add_subdirectory(docs)
endif ()

# We are only testing the procedural
if (BUILD_TESTSUITE AND BUILD_PROCEDURAL)
    enable_testing()
    add_subdirectory(testsuite)
endif ()

if (NOT BUILD_WITH_USD_STATIC)
    if (BUILD_RENDER_DELEGATE OR BUILD_NDR_PLUGIN OR BUILD_USD_IMAGING_PLUGIN OR BUILD_SCENE_INDEX_PLUGIN)
        install(FILES plugInfo.json
            DESTINATION "${PREFIX_PLUGINS}")
    endif ()
    if (BUILD_BUNDLE)
        install(FILES plugInfo.json
            DESTINATION "${PREFIX_BUNDLE}")
    endif()
endif()

install(FILES LICENSE.md DESTINATION .)
