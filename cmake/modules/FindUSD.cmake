# Simple module to find USD vanilla or within DCCs

set(USD_LIB_EXTENSION ${CMAKE_SHARED_LIBRARY_SUFFIX} CACHE STRING "Extension of USD libraries")
set(USD_STATIC_LIB_EXTENSION ${CMAKE_STATIC_LIBRARY_SUFFIX} CACHE STRING "Extension of USD libraries")

# A function to find the USD version in the header file instead of relying PXR_VERSION
# Should return with USD_VERSION properly set
function(find_usd_version USD_INCLUDE_DIR)
    find_file(pxr_HEADER pxr/pxr.h PATHS ${USD_INCLUDE_DIR})
    foreach (_usd_comp MAJOR MINOR PATCH)
        file(STRINGS
            ${pxr_HEADER}
            _usd_tmp
            REGEX "#define PXR_${_usd_comp}_VERSION .*$")
        string(REGEX MATCHALL "[0-9]+" USD_${_usd_comp}_VERSION ${_usd_tmp})
    endforeach ()
    set(USD_VERSION ${USD_MAJOR_VERSION}.${USD_MINOR_VERSION}.${USD_PATCH_VERSION} PARENT_SCOPE)
endfunction()

# Function to check if usd was compiled with python support
#  Returns with USD_HAS_PYTHON 
function(check_usd_use_python)
    find_file(pxr_HEADER pxr.h PATHS ${USD_INCLUDE_DIRS})
    file(STRINGS
        ${pxr_HEADER}
        _usd_python_tmp
        NEWLINE_CONSUME
        REGEX "#if 1\n#define PXR_PYTHON_SUPPORT_ENABLED")
    if (_usd_python_tmp)
        set(USD_HAS_PYTHON ON PARENT_SCOPE)
    else ()
        set(USD_HAS_PYTHON OFF PARENT_SCOPE)
    endif ()
endfunction()


# 
macro(setup_usd_python)
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
endmacro()

macro(check_compositor)
    if (EXISTS "${USD_INCLUDE_DIR}/pxr/imaging/hdx/compositor.h")
        file(STRINGS
            "${USD_INCLUDE_DIR}/pxr/imaging/hdx/compositor.h"
            _usd_tmp
            REGEX "UpdateColor\([^)]*\)")
        # Check if `HdFormat format` is in the found string.
        if ("${_usd_tmp}" MATCHES ".*HdFormat format.*")
            set(USD_HAS_UPDATED_COMPOSITOR ON)
        endif ()
    endif ()
endmacro()

if (MAYA_LOCATION AND MAYAUSD_LOCATION)
    message(STATUS "Looking for USD maya")
    # We need to search for the python libraries here as pxrConfig embed a variable PYTHON_LIBRARIES;
    list(APPEND CMAKE_FRAMEWORK_PATH ${MAYA_LOCATION}/Contents/Frameworks)
    # TODO Windows and Linux
    # Looking for the python shipped with Mayas
    message(STATUS "Using hint: Python3_INCLUDE_DIR: ${Python3_INCLUDE_DIR}")
    message(STATUS "Using hint: Python3_LIBRARY: ${Python3_LIBRARY}")
    message(STATUS "Using hint: Python3_EXECUTABLE: ${Python3_EXECUTABLE}")
    find_package(Python3 COMPONENTS Development Interpreter REQUIRED)
    if (NOT Python3_FOUND)
        message(WARNING "Python for maya not found")
    else()
        # Setting PYTHON_LIBRARIES for the pxrTargets shipped with mayausd
        set(PYTHON_LIBRARIES ${Python3_LIBRARIES})
        set(USD_HAS_PYTHON true)

        # TODO check linux and windows
        find_file(PYTHON_EXECUTABLE 
            NAME mayapy
            HINTS ${MAYA_LOCATION}/Contents/bin
            DOC "USD Gen Schema executable"
        )
    endif()
    message(STATUS "Using maya PYTHON include dirs: ${Python3_INCLUDE_DIRS}")
    # mayausd needs a variable PXR_USD_LOCATION to work properly, and it needs to be searched before the vanilla usd
    # otherwise the makefile trips up. We expect USD_LOCATION to point at the root of maya usd.
    # For maya usd, we need maya and mayausd as they are provided separately
    set(PXR_USD_LOCATION ${USD_LOCATION}) # TODO change since now the version is added to USD -> USD_2411
    find_package(pxr PATHS ${PXR_USD_LOCATION} ${MAYAUSD_LOCATION})
    if (pxr_FOUND)
        set(USD_LOCATION ${PXR_USD_LOCATION})
        message(STATUS "Found Maya USD in ${MAYAUSD_LOCATION}")

        # Set USD_VERSION
        find_usd_version(${PXR_INCLUDE_DIRS}) 
        message(STATUS "USD version ${USD_VERSION}")

        # The mayausd libraries are only x86_64 on osx for the moment
        set(CMAKE_OSX_ARCHITECTURES x86_64;arm64)

        set(USD_MONOLITHIC_BUILD OFF)

        # Variable for running usdGenSchema
        # USD_LIBRARY_DIR is needed by the schema script
        set(USD_LIBRARY_DIR ${PXR_USD_LOCATION}/lib)
        set(USD_INCLUDE_DIR ${PXR_INCLUDE_DIRS})
        find_file(USD_GENSCHEMA
            NAMES usdGenSchema
            PATHS "${PXR_USD_LOCATION}/bin"
            DOC "USD Gen Schema executable")

        unset(PXR_USD_LOCATION)
        return()
    else()
        message(WARNING "pxrConfig.cmake was not found in ${PXR_USD_LOCATION} or ${MAYAUSD_LOCATION}")
    endif()
    unset(PXR_USD_LOCATION)
else()
    if (MAYAUSD_LOCATION OR MAYA_LOCATION)
        message(FATAL_ERROR "MAYA_LOCATION and MAYAUSD_LOCATION needs to be both defined")
    endif()
endif()

# First we look for a pxrConfig file as it normally has all the knowledge of how USD was compiled and the required
# dependencies.
find_package(pxr PATHS ${USD_LOCATION})
if (pxr_FOUND)
    # If we have found a pxrConfig file, we just set our USD_* variables to the pxr_* ones
    message(STATUS "Pixar USD ${PXR_VERSION} found - using pxrConfig.cmake")
    set(USD_INCLUDE_DIR ${PXR_INCLUDE_DIRS})
    message(STATUS "USD include dir: ${USD_INCLUDE_DIR}")

    find_usd_version(${USD_INCLUDE_DIR})

    # Assuming that if we have the targets usd_m?, then usd was compiled as a monolithic lib
    if (TARGET usd_ms OR TARGET usd_m)
        set(USD_MONOLITHIC_BUILD ON)
    endif()

    # Check we have usd_m, as the standard pxrConfig doesn't define it
    if (BUILD_WITH_USD_STATIC AND NOT TARGET usd_m)
        message(STATUS "usd_m static lib not defined in pxrConfig.cmake, attempting to find it")
        find_library(USD_usd_m_LIBRARY
            NAMES libusd_m.a libusd_m.lib
            HINTS ${USD_LOCATION}/lib)
        if (USD_usd_m_LIBRARY)
            add_library(usd_m INTERFACE IMPORTED)
            set_target_properties(usd_m
                PROPERTIES
                IMPORTED_LOCATION ${USD_usd_m_LIBRARY})
        else()
            message(FATAL_ERROR "usd_m static monolithic lib not found")
        endif ()
    endif()

    if (BUILD_WITH_USD_STATIC AND USD_MONOLITHIC_BUILD)
        # For the static build, we want to split the static libs and the shared ones
        # as the static will be linked as whole archive
        # First get the dependencies of the shared monolithic usd library
        get_property(USD_M_TRANSITIVE_LIBS_ TARGET usd_ms PROPERTY INTERFACE_LINK_LIBRARIES)
        # Filter the static
        foreach(USD_M_LIB ${USD_M_TRANSITIVE_LIBS_})
            if(${USD_M_LIB} MATCHES "${USD_STATIC_LIB_EXTENSION}$")
                list(APPEND USD_TRANSITIVE_STATIC_LIBS ${USD_M_LIB})
            else()
                list(APPEND USD_TRANSITIVE_SHARED_LIBS ${USD_M_LIB})
            endif()
        endforeach()
    endif()
    check_usd_use_python()
    # Ideally USD should export the python includes and libs
    setup_usd_python()

    check_compositor()

    # usdGenSchema
    find_file(USD_GENSCHEMA
        NAMES usdGenSchema
        PATHS "${USD_LOCATION}/bin"
        DOC "USD Gen Schema executable")
    set(USD_LIBRARY_DIR ${USD_LOCATION}/lib)

    if (USD_INCLUDE_DIR AND EXISTS "${USD_INCLUDE_DIR}/pxr/imaging/hdx/fullscreenShader.h")
        set(USD_HAS_FULLSCREEN_SHADER ON)
    endif ()
    return()

else()
    message(STATUS "Vanilla USD not found")
endif()

# If we are looking for Houdini USD
if (HOUDINI_LOCATION)
    message(STATUS "Looking for houdini USD")
    set(USD_LOCATION ${HOUDINI_LOCATION})
    find_package(Houdini PATHS ${USD_LOCATION}/toolkit/cmake ${HOUDINI_LOCATION}/Frameworks/Houdini.framework/Versions/Current/Resources/toolkit/cmake)
    if (Houdini_FOUND)
        message(STATUS "Found Houdini, using houdini's USD")
        # We extract the include dir from the houdini target
        get_property(USD_INCLUDE_DIR TARGET Houdini PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
        message(STATUS "Houdini include dirs: ${USD_INCLUDE_DIR}")

        # We extract the python version from the HoudiniConfig.cmake variable
        set(HOUDINI_PYTHON_VERSION ${_houdini_python_version})
        message(STATUS "Houdini python version: ${HOUDINI_PYTHON_VERSION}")
        
        # This will require a license
        #houdini_get_default_install_dir(HOUDINI_DEFAULT_INSTALL_DIR)
        #message(STATUS "Houdini default install dir: ${HOUDINI_DEFAULT_INSTALL_DIR}")

        # Look for the usd version
        find_usd_version(${USD_INCLUDE_DIR})
        message(STATUS "USD version: ${USD_VERSION}")

        # List of usd libraries we need for this project
        set(ARNOLD_USD_LIBS_ arch;tf;gf;vt;sdr;sdf;usd;plug;trace;work;hf;hd;usdImaging;usdLux;pxOsd;cameraUtil;ar;usdGeom;usdShade;pcp;usdUtils;usdVol;usdSkel;usdRender;js)
        if (${USD_VERSION} VERSION_LESS "0.25.05")
            list(APPEND ARNOLD_USD_LIBS_ ndr)
        endif()
        if (${USD_VERSION} VERSION_GREATER_EQUAL "0.25.05")
            list(APPEND ARNOLD_USD_LIBS_ hdsi;usdSkelImaging;ts)
        endif()
        if (APPLE)
           set(HOUDINI_LIBS_LOCATION ${HOUDINI_LOCATION}/Frameworks/Houdini.framework/Versions/Current/Libraries)
        elseif(UNIX)
           set(HOUDINI_LIBS_LOCATION ${HOUDINI_LOCATION}/dsolib)
        else()
           set(HOUDINI_LIBS_LOCATION ${HOUDINI_LOCATION}/bin)
        endif()
        foreach (lib ${ARNOLD_USD_LIBS_})
            # We alias standard usd targets to the Houdini ones if they are defined, hopefully this will allow to follow their path change
            # without modifying our code
            if (TARGET Houdini::Dep::pxr_${lib})
                add_library(${lib} ALIAS Houdini::Dep::pxr_${lib})
            else() # otherwise we pick the library on disk, but the location  might change in the future
                add_library(${lib} SHARED IMPORTED)
                set_property(TARGET ${lib} PROPERTY IMPORTED_LOCATION "${HOUDINI_LIBS_LOCATION}/libpxr_${lib}${CMAKE_SHARED_LIBRARY_SUFFIX}")
            endif()
        endforeach ()
        if (APPLE)
            set(USD_TRANSITIVE_SHARED_LIBS "-Wl,-F${HOUDINI_LOCATION}/Frameworks" "-framework Houdini" "-framework Python")
        else()
            set(USD_TRANSITIVE_SHARED_LIBS Houdini::Dep::python${HOUDINI_PYTHON_VERSION};Houdini::Dep::tbb;Houdini::Dep::tbbmalloc)
            if (${USD_VERSION} VERSION_LESS "0.25.05")
                list(APPEND USD_TRANSITIVE_SHARED_LIBS Houdini::Dep::hboost_python)
            endif()
        endif()
        
        check_compositor()

        # usdGenSchema
        find_file(USD_GENSCHEMA
            NAMES usdGenSchema
            PATHS "${HOUDINI_LOCATION}/bin" "${HOUDINI_LOCATION}/Frameworks/Houdini.framework//Versions/Current/Resources/bin"
            DOC "USD Gen Schema executable")

        check_usd_use_python() # should that be true by default on houdini ?
        
        # Setting the python executable as hython. (This will require a license)
        set(PYTHON_EXECUTABLE "${_houdini_bin_dir}/hython${CMAKE_EXECUTABLE_SUFFIX}" )

        return()
    else()
        message(STATUS "Houdini USD not found")
    endif()
endif()


message(STATUS "Looking for USD libraries and includes in ${USD_LOCATION}")

# This is a list of directories to search in for the usd libraries
set(USD_LIBRARY_DIR_HINTS "${USD_LOCATION}/lib")

if (DEFINED USD_LIBRARY_DIR)
    list(APPEND USD_LIBRARY_DIR_HINTS  "${USD_LIBRARY_DIR}")
endif()

if (DEFINED $ENV{USD_LOCATION})
    list(PREPEND USD_LIBRARY_DIR_HINTS  "$ENV{USD_LOCATION}/lib")
endif()

# This is a list of library postfix we expect in the library directory, this is basically the name
# of potential libraries without the prefix
set(USD_LIBRARY_EXT_HINTS usd${USD_LIB_EXTENSION} usd_ms${USD_LIB_EXTENSION} usd_m${USD_STATIC_LIB_EXTENSION})

# If the user didn't set the USD_LIB_PREFIX, we try to deduce it
if (NOT DEFINED USD_LIB_PREFIX)
    message(STATUS "USD_LIB_PREFIX is not defined, we are trying to find it")
    foreach (SEARCH_PATH IN ITEMS ${USD_LIBRARY_DIR_HINTS})
        foreach (USD_LIBRARY_EXT IN ITEMS ${USD_LIBRARY_EXT_HINTS})
            file(GLOB FOUND_USD_LIB RELATIVE "${SEARCH_PATH}" "${SEARCH_PATH}/*${USD_LIBRARY_EXT}" )
            if (FOUND_USD_LIB)
                string(FIND  "${FOUND_USD_LIB}" "${USD_LIBRARY_EXT}" USD_PREFIX_LENGTH )
                string(SUBSTRING "${FOUND_USD_LIB}" 0 ${USD_PREFIX_LENGTH} USD_LIB_PREFIX_FOUND)
                break()
            endif()
        endforeach()
        if (DEFINED USD_LIB_PREFIX_FOUND)
            message(STATUS "Found USD_LIB_PREFIX: ${USD_LIB_PREFIX}")
            break()
        endif()
    endforeach()
else ()
    if (WIN32)
        set(USD_LIB_PREFIX_FOUND "")
    else ()
        set(USD_LIB_PREFIX_FOUND lib)
    endif ()
endif ()

set(USD_LIB_PREFIX ${USD_LIB_PREFIX_FOUND} CACHE STRING "Prefix of USD libraries")


find_path(USD_INCLUDE_DIR pxr/pxr.h
    PATHS "${USD_INCLUDE_DIR}"
    "${USD_LOCATION}/include"
    "$ENV{USD_LOCATION}/include"
    DOC "USD Include directory")

# We need to find either usd or usd_ms, with taking the prefix into account.
find_path(USD_LIBRARY_DIR
    NAMES ${USD_LIB_PREFIX}usd${USD_LIB_EXTENSION}
    ${USD_LIB_PREFIX}usd_ms${USD_LIB_EXTENSION}
    ${USD_LIB_PREFIX}usd_m${USD_STATIC_LIB_EXTENSION}
    PATHS ${USD_LIBRARY_DIR_HINTS}
    DOC "USD Libraries directory")


find_file(USD_GENSCHEMA
    NAMES usdGenSchema
    PATHS "${USD_BINARY_DIR}"
    "$ENV{USD_BINARY_DIR}"
    "${USD_LOCATION}/bin"
    "$ENV{USD_LOCATION}/bin"
    DOC "USD Gen Schema executable")

find_file(USD_RECORD
    NAMES usdrecord
          usdRecord
    PATHS "${USD_BINARY_DIR}"
    "$ENV{USD_BINARY_DIR}"
    "${USD_LOCATION}/bin"
    "$ENV{USD_LOCATION}/bin"
    DOC "USD Gen Schema executable")

# We attempt to locate the USD binary dir by looking for a few usual suspects.
find_path(USD_BINARY_DIR
    NAMES usdcat usddiff usdview
    PATHS "${USD_BINARY_DIR}"
    "$ENV{USD_BINARY_DIR}"
    "${USD_LOCATION}/bin"
    "$ENV{USD_LOCATION}/bin"
    DOC "Path to USD binaries.")

# USD Maya components

find_path(USD_MAYA_INCLUDE_DIR usdMaya/api.h
    PATHS "${USD_LOCATION}/third_party/maya/include"
    "$ENV{USD_LOCATION}/third_party/maya/include"
    "${USD_MAYA_ROOT}/third_party/maya/include"
    "$ENV{USD_MAYA_ROOT}/third_party/maya/include"
    DOC "USD Maya Include directory")

find_path(USD_MAYA_LIBRARY_DIR
    NAMES ${USD_LIB_PREFIX}usdMaya${USD_LIB_EXTENSION}
    PATHS "${USD_LOCATION}/third_party/maya/lib"
    "$ENV{USD_LOCATION}/third_party/maya/lib"
    "${USD_MAYA_ROOT}/third_party/maya/lib"
    "$ENV{USD_MAYA_ROOT}/third_party/maya/lib"
    DOC "USD Maya Library directory")


if (USD_INCLUDE_DIR AND EXISTS "${USD_INCLUDE_DIR}/pxr/pxr.h")
    foreach (_usd_comp MAJOR MINOR PATCH)
        file(STRINGS
            "${USD_INCLUDE_DIR}/pxr/pxr.h"
            _usd_tmp
            REGEX "#define PXR_${_usd_comp}_VERSION .*$")
        string(REGEX MATCHALL "[0-9]+" USD_${_usd_comp}_VERSION ${_usd_tmp})
    endforeach ()
    check_compositor()
    file(STRINGS
        "${USD_INCLUDE_DIR}/pxr/pxr.h"
        _usd_python_tmp
        NEWLINE_CONSUME
        REGEX "#if 1\n#define PXR_PYTHON_SUPPORT_ENABLED")
    if (_usd_python_tmp)
        set(USD_HAS_PYTHON ON)
    else ()
        set(USD_HAS_PYTHON OFF)
    endif ()
    set(USD_VERSION ${USD_MAJOR_VERSION}.${USD_MINOR_VERSION}.${USD_PATCH_VERSION})
endif ()

if (USD_INCLUDE_DIR AND EXISTS "${USD_INCLUDE_DIR}/pxr/imaging/hdx/fullscreenShader.h")
    set(USD_HAS_FULLSCREEN_SHADER ON)
endif ()

# Look for the dynamic libraries.
# Right now this is using a hardcoded list of libraries, but in the future we should parse the installed cmake files
# and figure out the list of the names for libraries.
set(USD_LIBS ar;arch;cameraUtil;garch;gf;glf;hd;hdMtlx;hdSt;hdx;hf;hgi;hgiGL;hgInterop;hio;js;kind;pcp;plug;pxOsd;sdf;sdr;tf;trace;usd;usdAppUtils;usdGeom;usdHydra;usdImaging;usdImagingGL;usdLux;usdMedia;usdRender;usdRi;usdRiImaging;usdShade;usdSkel;usdSkelImaging;usdUI;usdUtils;usdviewq;usdVol;usdVolImaging;vt;work;usd_ms)
if (${USD_VERSION} VERSION_LESS "0.25.05")
    list(APPEND USD_LIBS ndr)
endif()
foreach (lib ${USD_LIBS})
    find_library(USD_${lib}_LIBRARY
        NAMES ${USD_LIB_PREFIX}${lib}${USD_LIB_EXTENSION}
        HINTS ${USD_LIBRARY_DIR})
    if (USD_${lib}_LIBRARY)
        add_library(${lib} INTERFACE IMPORTED)
        set_target_properties(${lib}
            PROPERTIES
            INTERFACE_LINK_LIBRARIES ${USD_${lib}_LIBRARY}
            )
        list(APPEND USD_LIBRARIES ${USD_${lib}_LIBRARY})
    endif ()
endforeach ()

# Look for the static library.
find_library(USD_usd_m_LIBRARY
    NAMES ${USD_LIB_PREFIX}usd_m${USD_STATIC_LIB_EXTENSION}
    HINTS ${USD_LIBRARY_DIR})
if (USD_usd_m_LIBRARY)
    add_library(usd_m INTERFACE IMPORTED)
    set_target_properties(usd_m
        PROPERTIES
        INTERFACE_LINK_LIBRARIES ${USD_usd_m_LIBRARY})
    list(APPEND USD_LIBRARIES ${USD_usd_m_LIBRARY})
endif ()

set(USD_MAYA_LIBS px_vp20;pxrUsdMayaGL;usdMaya)

foreach (lib ${USD_MAYA_LIBS})
    find_library(USD_MAYA_${lib}_LIBRARY
        NAMES ${USD_LIB_PREFIX}${lib}${USD_LIB_EXTENSION}
        HINTS ${USD_MAYA_LIBRARY_DIR})
    if (USD_MAYA_${lib}_LIBRARY)
        add_library(${lib} INTERFACE IMPORTED)
        set_target_properties(${lib}
            PROPERTIES
            INTERFACE_LINK_LIBRARIES ${USD_MAYA_${lib}_LIBRARY}
            )
        list(APPEND USD_MAYA_LIBRARIES ${USD_MAYA_${lib}_LIBRARY})
    endif ()
endforeach ()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(USD
    REQUIRED_VARS
    USD_INCLUDE_DIR
    USD_LIBRARY_DIR
    USD_LIBRARIES
    VERSION_VAR
    USD_VERSION)

setup_usd_python()

# Boost has been removed since 25.05
if (${USD_VERSION} VERSION_LESS "0.25.05")
    # TODO: BUILD_CUSTOM_BOOST should be removed from the cmake build
    if (NOT BUILD_USE_CUSTOM_BOOST)
        # Forcing SHARED_LIBS: See here: https://github.com/boostorg/boost_install/issues/5
        set(BUILD_SHARED_LIBS ON)
        if (USD_HAS_PYTHON)
            find_package(Boost COMPONENTS python REQUIRED PATHS ${USD_LOCATION})
        else ()
            find_package(Boost REQUIRED PATHS ${USD_LOCATION})
        endif ()
    endif ()


    # This disables explicit linking from boost headers on Windows.
    if (BUILD_BOOST_ALL_NO_LIB AND WIN32)
        add_compile_definitions(BOOST_ALL_NO_LIB=1)
        # This is for Houdini's boost libs. TODO: should that go up ? in the houdini search
        add_compile_definitions(HBOOST_ALL_NO_LIB=1)
    endif ()
endif ()

# TBB
find_package(TBB REQUIRED)
if (TBB_STATIC_BUILD)
    list(APPEND USD_TRANSITIVE_STATIC_LIBS ${TBB_LIBRARIES})
else ()
    list(APPEND USD_TRANSITIVE_SHARED_LIBS ${TBB_LIBRARIES})
endif ()
