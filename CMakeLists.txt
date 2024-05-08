cmake_minimum_required(VERSION 3.20)

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

if (BUILD_TESTSUITE)
    enable_testing()
    add_subdirectory(testsuite)
endif ()

if (NOT BUILD_WITH_USD_STATIC)
    if (BUILD_RENDER_DELEGATE OR BUILD_NDR_PLUGIN OR BUILD_USD_IMAGING_PLUGIN OR BUILD_SCENE_DELEGATE)
        install(FILES plugInfo.json
            DESTINATION "${PREFIX_PLUGINS}")
    endif ()
endif()

install(FILES LICENSE.md DESTINATION .)
