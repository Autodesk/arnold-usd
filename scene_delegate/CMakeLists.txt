set(SRC
    adapter_registry.cpp
    persp_camera_adapter.cpp
    polymesh_adapter.cpp
    prim_adapter.cpp
    rprim_adapter.cpp
    scene_delegate.cpp)

set(HDR
    adapter_registry.h
    persp_camera_adapter.h
    polymesh_adapter.h
    prim_adapter.h
    rprim_adapter.h
    api.h
    scene_delegate.h)

add_library(imagingArnold SHARED ${COMMON_SRC} ${SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(imagingArnold PRIVATE ${COMMON_HDR} ${HDR})
endif ()
add_common_dependencies(
    TARGET_NAME imagingArnold
    USD_DEPENDENCIES arch plug tf trace vt gf work sdf hf hd)
target_compile_definitions(imagingArnold PRIVATE "IMAGINGARNOLD_EXPORTS=1")

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
