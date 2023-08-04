
set(SRC
    adapter_registry.cpp
    delegate.cpp
    delegate_proxy.cpp
    persp_camera_adapter.cpp
    polymesh_adapter.cpp
    prim_adapter.cpp
    rprim_adapter.cpp
    )

set(HDR
    adapter_registry.h
    api.h
    delegate.h
    delegate_proxy.h
    persp_camera_adapter.h
    polymesh_adapter.h
    prim_adapter.h
    rprim_adapter.h)


if (NOT USD_STATIC_BUILD)
    add_library(imagingArnold SHARED ${COMMON_SRC} ${SRC})
    if (BUILD_HEADERS_AS_SOURCES)
        target_sources(imagingArnold PRIVATE ${COMMON_HDR} ${HDR})
    endif ()
    add_common_dependencies(
        TARGET_NAME imagingArnold
        USD_DEPENDENCIES arch js plug tf trace vt gf work sdf hf hd)
    target_compile_definitions(imagingArnold PRIVATE "IMAGINGARNOLD_EXPORTS=1")
    target_link_libraries(imagingArnold PRIVATE common)

    set_target_properties(imagingArnold PROPERTIES PREFIX "")

    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/../common PREFIX common FILES ${COMMON_SRC} ${COMMON_HDR})

    set(PLUGINFO_OUT "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")

    configure_file(
        plugInfo.json.in
        "${PLUGINFO_OUT}"
    )

    install(TARGETS imagingArnold
        DESTINATION "${PREFIX_PLUGINS}")

    install(FILES ${PLUGINFO_OUT}
        DESTINATION "${PREFIX_PLUGINS}/imagingArnold/resources")

    #install(FILES ${HDR}
    #    DESTINATION "${PREFIX_HEADERS}/arnold_usd/scene_delegate")
endif()
