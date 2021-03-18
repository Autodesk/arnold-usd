set(SRC
    native_gprim_adapter.cpp
    usd_lux_light_filter_adapter.cpp)

set(HDR
    api.h
    native_gprim_adapter.h
    usd_lux_light_filter_adapter.h)

add_library(usdImagingArnold SHARED ${SRC} ${COMMON_SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(usdImagingArnold PRIVATE ${HDR} ${COMMON_HDR})
endif ()
target_include_directories(usdImagingArnold SYSTEM PUBLIC "${USD_INCLUDE_DIR}")
target_include_directories(usdImagingArnold SYSTEM PUBLIC "${ARNOLD_INCLUDE_DIR}")
target_include_directories(usdImagingArnold SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
target_include_directories(usdImagingArnold SYSTEM PUBLIC "${TBB_INCLUDE_DIRS}")
target_include_directories(usdImagingArnold SYSTEM PUBLIC "${Python2_INCLUDE_DIRS}")
target_include_directories(usdImagingArnold PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(usdImagingArnold PRIVATE "USDIMAGINGARNOLD_EXPORTS=1")
target_compile_definitions(hdArnold PRIVATE "ARNOLDUSD_EXPORTS=1")

target_link_libraries(usdImagingArnold PUBLIC "${ARNOLD_LIBRARY}" "${TBB_LIBRARIES}")
if (USD_HAS_PYTHON)
    target_link_libraries(usdImagingArnold PUBLIC "${Boost_LIBRARIES}" Python2::Python)
endif ()

if (USD_MONOLITHIC_BUILD)
    target_link_libraries(usdImagingArnold PUBLIC usd_ms)
else ()
    target_link_libraries(usdImagingArnold PUBLIC arch plug tf vt gf work sdf)
    target_link_libraries(usdImagingArnold PUBLIC hf hd hdx usd usdGeom usdImaging usdLux usdShade)
endif ()

if (LINUX)
    target_link_libraries(usdImagingArnold PUBLIC dl)
endif ()

set_target_properties(usdImagingArnold PROPERTIES PREFIX "")

set(PLUGINFO_OUT "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")

configure_file(
    plugInfo.json.in
    "${PLUGINFO_OUT}"
)

install(TARGETS usdImagingArnold
    DESTINATION "${PREFIX_PLUGINS}")

install(FILES ${PLUGINFO_OUT}
    DESTINATION "${PREFIX_PLUGINS}/usdImagingArnold/resources")

install(FILES ${HDR}
    DESTINATION "${PREFIX_HEADERS}/usd_imaging")
