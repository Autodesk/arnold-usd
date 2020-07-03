# Simple module to find Katana

find_path(KATANA_INCLUDE_DIR
          NAMES FnAPI/FnAPI.h
          HINTS "${KATANA_INCLUDE_DIR}" "$ENV{KATANA_INCLUDE_DIR}"
                "${KATANA_LOCATION}/plugin_apis/include" "$ENV{KATANA_LOCATION}/plugin_apis/include")

find_path(KATANA_SOURCE_DIR
          NAMES FnConfig/FnConfig.cpp
          HINTS "${KATANA_SOURCE_DIR}" "$ENV{KATANA_SOURCE_DIR}"
                "${KATANA_LOCATION}/plugin_apis/src" "$ENV{KATANA_LOCATION}/plugin_apis/src")

if (NOT WIN32)
    find_library(KATANA_CEL_LIBRARY
                 NAMES CEL
                 HINTS "${KATANA_BINARY_DIR}" "$ENV{KATANA_BINARY_DIR}"
                       "${KATANA_LOCATION}/bin" "$ENV{KATANA_LOCATION}/bin")
endif ()

if (KATANA_INCLUDE_DIR AND EXISTS "${KATANA_INCLUDE_DIR}/FnAPI/FnAPI.h")
    foreach (_katana_comp MAJOR MINOR RELEASE)
        file(STRINGS
             "${KATANA_INCLUDE_DIR}/FnAPI/FnAPI.h"
             _katana_tmp
             REGEX "#define KATANA_VERSION_${_katana_comp} .*$")
        string(REGEX MATCHALL "[0-9]+" KATANA_VERSION_${_katana_comp} ${_katana_tmp})
    endforeach ()
    set(KATANA_VERSION ${KATANA_VERSION_MAJOR}.${KATANA_VERSION_MINOR}.${KATANA_VERSION_RELEASE})
endif ()

include(FindPackageHandleStandardArgs)

if (WIN32)
    set(_required_vars KATANA_INCLUDE_DIR KATANA_SOURCE_DIR)
else ()
    set(_required_vars KATANA_INCLUDE_DIR KATANA_SOURCE_DIR KATANA_CEL_LIBRARY)
endif ()

find_package_handle_standard_args(KATANA
    REQUIRED_VARS
        ${_required_vars}
    VERSION_VAR
        KATANA_VERSION)
