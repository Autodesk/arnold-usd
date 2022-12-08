# Simple module to find USD.

if (LINUX)
    set(USD_LIB_EXTENSION ".so"
        CACHE STRING "Extension of USD libraries")
elseif (WIN32)
    set(USD_LIB_EXTENSION ".lib"
        CACHE STRING "Extension of USD libraries")
else () # MacOS
    set(USD_LIB_EXTENSION ".dylib"
        CACHE STRING "Extension of USD libraries")
endif ()

if (WIN32)
    set(USD_STATIC_LIB_EXTENSION ".lib"
        CACHE STRING "Extension of the static USD libraries")
else () # MacOS / Linux
    set(USD_STATIC_LIB_EXTENSION ".a"
        CACHE STRING "Extension of the static USD libraries")
endif ()


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
    message(STATUS "USD_LIB_PREFIX is not defined, we are now trying to find it")
    foreach (SEARCH_PATH IN ITEMS ${USD_LIBRARY_DIR_HINTS})
        foreach (USD_LIBRARY_EXT IN ITEMS ${USD_LIBRARY_EXT_HINTS})
            message(STATUS "${SEARCH_PATH} ${USD_LIBRARY_EXT}")
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

if (WIN32)
    set(USD_SCRIPT_EXTENSION ".cmd"
        CACHE STRING "Extension of USD scripts")
else ()
    set(USD_SCRIPT_EXTENSION ""
        CACHE STRING "Extension of USD scripts")
endif ()

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
set(USD_LIBS ar;arch;cameraUtil;garch;gf;glf;hd;hdMtlx;hdSt;hdx;hf;hgi;hgiGL;hgInterop;hio;js;kind;ndr;pcp;plug;pxOsd;sdf;sdr;tf;trace;usd;usdAppUtils;usdGeom;usdHydra;usdImaging;usdImagingGL;usdLux;usdMedia;usdRender;usdRi;usdRiImaging;usdShade;usdSkel;usdSkelImaging;usdUI;usdUtils;usdviewq;usdVol;usdVolImaging;vt;work;usd_ms)

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
