set(SRC
    nodes/driver_aov.cpp
    nodes/driver_main.cpp
    nodes/nodes.cpp

    basis_curves.cpp
    camera.cpp
    config.cpp
    debug_codes.cpp
    instancer.cpp
    light.cpp
    material.cpp
    material_tracker.cpp
    mesh.cpp
    native_rprim.cpp
    openvdb_asset.cpp
    points.cpp
    render_buffer.cpp
    render_delegate.cpp
    render_param.cpp
    render_pass.cpp
    renderer_plugin.cpp
    shape.cpp
    utils.cpp
    volume.cpp
    )

set(HDR
    api.h
    basis_curves.h
    camera.h
    config.h
    debug_codes.h
    rprim.h
    hdarnold.h
    instancer.h
    light.h
    material.h
    material_tracker.h
    mesh.h
    native_rprim.h
    openvdb_asset.h
    points.h
    render_buffer.h
    render_delegate.h
    render_param.h
    render_pass.h
    renderer_plugin.h
    shape.h
    utils.h
    volume.h
    )

add_library(hdArnold SHARED ${COMMON_SRC} ${SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(hdArnold PRIVATE ${COMMON_HDR} ${HDR})
endif ()
add_common_dependencies(
    TARGET_NAME hdArnold
    USD_DEPENDENCIES arch plug tf vt gf work sdf
    hf hd hdx usdImaging usdLux pxOsd cameraUtil)
target_compile_definitions(hdArnold PRIVATE "HDARNOLD_EXPORTS=1")

if (${USD_HAS_UPDATED_COMPOSITOR})
    target_compile_definitions(hdArnold PUBLIC "USD_HAS_UPDATED_COMPOSITOR=1")
endif ()

if (${USD_HAS_FULLSCREEN_SHADER})
    target_compile_definitions(hdArnold PUBLIC "USD_HAS_FULLSCREEN_SHADER=1")
endif ()

set(PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")

if (${USD_VERSION} VERSION_GREATER "0.19.7")
    set(RENDERER_PLUGIN_BASE "HdRendererPlugin")
else ()
    set(RENDERER_PLUGIN_BASE "HdxRendererPlugin")
endif ()

configure_file(
    plugInfo.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
)

install(TARGETS hdArnold
    DESTINATION "${PREFIX_PLUGINS}")

install(FILES ${PLUGINFO}
    DESTINATION "${PREFIX_PLUGINS}/hdArnold/resources")

install(FILES ${HDR}
    DESTINATION "${PREFIX_HEADERS}/render_delegate")
