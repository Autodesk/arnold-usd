# Simple module to find Katana

if (LINUX)
    set(USDKATANA_LIB_EXTENSION ".so"
            CACHE STRING "Extension of USDKatana libraries")
else ()
    set(USDKATANA_LIB_EXTENSION ".lib"
            CACHE STRING "Extension of USDKatana libraries")
endif ()

if (WIN32)
    set(USDKATANA_LIB_PREFIX ""
            CACHE STRING "Prefix of USD libraries")
else ()
    set(USDKATANA_LIB_PREFIX lib
            CACHE STRING "Prefix of USD libraries")
endif ()


find_path(USDKATANA_INCLUDE_DIR
        NAMES usdKatana/api.h
        HINTS "${USDKATANA_INCLUDE_DIR}" "$ENV{USDKATANA_INCLUDE_DIR}"
              "${USDKATANA_LOCATION}/include" "$ENV{USDKATANA_LOCATION}/include"
              "${KATANA_LOCATION}/plugins/Resources/Usd/include" "$ENV{KATANA_LOCATION}/plugins/Resources/Usd/include")

set(USDKATANA_LIBS usdKatana;vtKatana)

foreach (lib ${USDKATANA_LIBS})
    find_library(USDKATANA_${lib}_LIBRARY
            NAMES ${USDKATANA_LIB_PREFIX}${lib}${USDKATANA_LIB_EXTENSION}
            HINTS "${USDKATANA_LIBRARY_DIR}" "$ENV{USDKATANA_LIBRARY_DIR}"
                  "${USDKATANA_LOCATION}/libs" "$ENV{USDKATANA_LOCATION}/libs"
                  "${KATANA_LOCATION}/plugins/Resources/Usd/lib" "$ENV{KATANA_LOCATION}/plugins/Resources/Usd/lib")
    if (USDKATANA_${lib}_LIBRARY)
        add_library(${lib} INTERFACE IMPORTED)
        set_target_properties(${lib}
                PROPERTIES
                INTERFACE_LINK_LIBRARIES ${USDKATANA_${lib}_LIBRARY}
                )
        list(APPEND USDKATANA_LIBRARIES ${USDKATANA_${lib}_LIBRARY})
    endif ()
endforeach ()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(USDKATANA
    REQUIRED_VARS
        USDKATANA_INCLUDE_DIR
        USDKATANA_LIBRARIES)
