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
    set(USD_LIB_PREFIX ""
        CACHE STRING "Prefix of USD libraries")
else ()
    set(USD_LIB_PREFIX lib
        CACHE STRING "Prefix of USD libraries")
endif ()

find_path(USD_INCLUDE_DIR pxr/pxr.h
          PATHS ${USD_INCLUDE_DIR}
          ${USD_LOCATION}/include
          $ENV{USD_LOCATION}/include
          DOC "USD Include directory")

# We need to find either usd or usd_ms, with taking the prefix into account.
find_path(USD_LIBRARY_DIR
          NAMES ${USD_LIB_PREFIX}usd${USD_LIB_EXTENSION} ${USD_LIB_PREFIX}usd_ms${USD_LIB_EXTENSION}
          PATHS ${USD_LIBRARY_DIR}
          ${USD_LOCATION}/lib
          $ENV{USD_LOCATION}/lib
          DOC "USD Libraries directory")

# USD Maya components

find_path(USD_MAYA_INCLUDE_DIR usdMaya/api.h
          PATHS ${USD_LOCATION}/third_party/maya/include
          $ENV{USD_LOCATION}/third_party/maya/include
          ${USD_MAYA_ROOT}/third_party/maya/include
          $ENV{USD_MAYA_ROOT}/third_party/maya/include
          DOC "USD Maya Include directory")

find_path(USD_MAYA_LIBRARY_DIR
          NAMES ${USD_LIB_PREFIX}usdMaya${USD_LIB_EXTENSION}
          PATHS ${USD_LOCATION}/third_party/maya/lib
          $ENV{USD_LOCATION}/third_party/maya/lib
          ${USD_MAYA_ROOT}/third_party/maya/lib
          $ENV{USD_MAYA_ROOT}/third_party/maya/lib
          DOC "USD Maya Library directory")

if(USD_INCLUDE_DIR AND EXISTS "${USD_INCLUDE_DIR}/pxr/pxr.h")
    foreach(_usd_comp MAJOR MINOR PATCH)
        file(STRINGS
             "${USD_INCLUDE_DIR}/pxr/pxr.h"
             _usd_tmp
             REGEX "#define PXR_${_usd_comp}_VERSION .*$")
        string(REGEX MATCHALL "[0-9]+" USD_${_usd_comp}_VERSION ${_usd_tmp})
    endforeach()
    set(USD_VERSION ${USD_MAJOR_VERSION}.${USD_MINOR_VERSION}.${USD_PATCH_VERSION})
endif()

set(USD_LIBS ar;arch;cameraUtil;garch;gf;glf;hd;hdSt;hdx;hf;hgi;hgiGL;hio;js;kind;ndr;pcp;plug;pxOsd;sdf;sdr;tf;trace;usd;usdAppUtils;usdGeom;usdHydra;usdImaging;usdImagingGL;usdLux;usdRi;usdShade;usdShaders;usdSkel;usdSkelImaging;usdUI;usdUtils;usdviewq;usdVol;usdVolImaging;vt;work;usd_ms)

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
