include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
set(COMMON_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/common_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/constant_strings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/parameters_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rendersettings_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shape_utils.cpp")

set(COMMON_HDR
    "${CMAKE_CURRENT_SOURCE_DIR}/api_adapter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/common_bits.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/common_utils.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/constant_strings.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/parameters_utils.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/procedural_reader.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/rendersettings_utils.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shape_utils.h")

add_library(common STATIC ${COMMON_SRC})


# The includes folders are the same for all the libs - factorize
target_include_directories(common SYSTEM PUBLIC "${USD_INCLUDE_DIR}")
target_include_directories(common SYSTEM PUBLIC "${ARNOLD_INCLUDE_DIR}")
target_include_directories(common SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
target_include_directories(common SYSTEM PUBLIC "${TBB_INCLUDE_DIRS}")
if (BUILD_USE_PYTHON3)
    target_include_directories(common SYSTEM PUBLIC "${Python3_INCLUDE_DIRS}")
else ()
    target_include_directories(common SYSTEM PUBLIC "${Python2_INCLUDE_DIRS}")
endif ()

#if (USD_STATIC_BUILD)
#    if (WIN32)
#        target_link_options(common INTERFACE "-WHOLEARCHIVE:${USD_usd_m_LIBRARY}")
#        target_link_libraries(common INTERFACE Ws2_32 Dbghelp Shlwapi advapi32)
#        target_link_libraries(common INTERFACE "${TBB_LIBRARIES}")
#    else ()
#        set(_staticlibs "${USD_usd_m_LIBRARY}")
#        if (TBB_STATIC_BUILD)
#            set(_staticlibs "${_staticlibs},${TBB_LIBRARIES}")
#        else ()
#            target_link_libraries(common INTERFACE "${TBB_LIBRARIES}")
#        endif ()
#        if (LINUX)
#            target_link_libraries(common INTERFACE dl pthread)
#            target_link_options(common INTERFACE "-Wl,--whole-archive,${_staticlibs},--no-whole-archive")
#        else () # APPLE
#            target_link_options(common INTERFACE "-Wl,-all_load,${_staticlibs},-noall_load")
#        endif ()
#    endif ()
#else ()
#    target_link_libraries(common INTERFACE "${TBB_LIBRARIES}")
#    if (USD_MONOLITHIC_BUILD)
#        target_link_libraries(common INTERFACE usd_ms)
#    else ()
#        if (LINUX)
#            target_link_libraries(common INTERFACE dl pthread)
#        endif ()
#        target_link_libraries(common INTERFACE gf sdf tf usd ar arch pcp vt work)
#        target_link_libraries(common INTERFACE usdGeom usdShade usdUtils usdLux usdVol usdSkel usdRender)
#    endif ()
#endif ()
#
#if (NOT WIN32)
#    set_target_properties(common PROPERTIES POSITION_INDEPENDENT_CODE ON)
#endif ()
