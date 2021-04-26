set(SRC
    adapter_registry.cpp
    scene_delegate.cpp)

set(HDR
    adapter_registry.h
    api.h
    scene_delegate.h)

add_library(imagingArnold SHARED ${COMMON_SRC} ${SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(imagingArnold PRIVATE ${COMMON_HDR} ${HDR})
endif ()
target_include_directories(imagingArnold SYSTEM PUBLIC "${USD_INCLUDE_DIR}")
target_include_directories(imagingArnold SYSTEM PUBLIC "${ARNOLD_INCLUDE_DIR}")
target_include_directories(imagingArnold SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
target_include_directories(imagingArnold SYSTEM PUBLIC "${TBB_INCLUDE_DIRS}")
target_include_directories(imagingArnold SYSTEM PUBLIC "${Python2_INCLUDE_DIRS}")
target_include_directories(imagingArnold PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(imagingArnold PRIVATE "USDIMAGINGARNOLD_EXPORTS=1")
target_compile_definitions(imagingArnold PRIVATE "ARNOLDUSD_EXPORTS=1")

target_link_libraries(imagingArnold PUBLIC "${ARNOLD_LIBRARY}" "${TBB_LIBRARIES}")
if (USD_HAS_PYTHON)
    target_link_libraries(imagingArnold PUBLIC "${Boost_LIBRARIES}" Python2::Python)
endif ()

if (USD_MONOLITHIC_BUILD)
    target_link_libraries(imagingArnold PUBLIC usd_ms)
else ()
    target_link_libraries(imagingArnold PUBLIC arch plug tf vt gf work sdf)
    target_link_libraries(imagingArnold PUBLIC hf hd hdx)
endif ()

if (LINUX)
    target_link_libraries(imagingArnold PUBLIC dl)
endif ()

set_target_properties(imagingArnold PROPERTIES PREFIX "")

set(PLUGINFO_OUT "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")

configure_file(
    plugInfo.json.in
    "${PLUGINFO_OUT}"
)

install(TARGETS imagingArnold
    DESTINATION "${PREFIX_PLUGINS}")

install(FILES ${PLUGINFO_OUT}
    DESTINATION "${PREFIX_PLUGINS}/imagingArnold/resources")

install(FILES ${HDR}
    DESTINATION "${PREFIX_HEADERS}/imaging")
