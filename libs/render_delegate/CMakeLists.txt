set(SRC
    nodes/driver_main.cpp
    nodes/nodes.cpp
    basis_curves.cpp
    camera.cpp
    config.cpp
    instancer.cpp
    light.cpp
    mesh.cpp
    native_rprim.cpp
    node_graph.cpp
    openvdb_asset.cpp
    options.cpp
    points.cpp
    procedural_custom.cpp
    reader.cpp
    render_buffer.cpp
    render_delegate.cpp
    render_param.cpp
    render_pass.cpp
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
    mesh.h
    native_rprim.h
    node_graph.h
    openvdb_asset.h
    options.h
    points.h
    procedural_custom.h
    render_buffer.h
    render_delegate.h
    render_param.h
    render_pass.h
    shape.h
    utils.h
    volume.h
    )

add_library(render_delegate STATIC EXCLUDE_FROM_ALL ${COMMON_SRC} ${SRC} )
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(render_delegate PRIVATE ${COMMON_HDR} ${HDR})
endif ()

add_common_includes(TARGET_NAME render_delegate DEPENDENCIES common)

if (${USD_HAS_UPDATED_COMPOSITOR})
    target_compile_definitions(render_delegate PUBLIC "USD_HAS_UPDATED_COMPOSITOR=1")
endif ()

if (${USD_HAS_FULLSCREEN_SHADER})
    target_compile_definitions(render_delegate PUBLIC "USD_HAS_FULLSCREEN_SHADER=1")
endif ()

target_link_libraries(render_delegate INTERFACE common)
if (ENABLE_SHARED_ARRAYS)
    target_compile_definitions(render_delegate PUBLIC ENABLE_SHARED_ARRAYS=1)
endif()
if (HYDRA_NORMALIZE_DEPTH)
    target_compile_definitions(render_delegate PUBLIC HYDRA_NORMALIZE_DEPTH=1)
endif()

target_compile_definitions(render_delegate PRIVATE "HDARNOLD_EXPORTS=1")
if (BUILD_SCENE_INDEX_PLUGIN)
    target_compile_definitions(render_delegate PUBLIC ENABLE_SCENE_INDEX=1)
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})
