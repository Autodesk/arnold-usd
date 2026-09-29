set(SRC
    nodes/driver_main.cpp
    nodes/nodes.cpp
    basis_curves.cpp
    camera.cpp
    config.cpp
    coord_sys.cpp
    gaussian_splat.cpp
    houdini_logger.cpp
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
    render_settings.cpp
    shape.cpp
	shared_arrays.cpp
    utils.cpp
    volume.cpp
    )

set(HDR
    api.h
    basis_curves.h
    camera.h
    config.h
    coord_sys.h
    debug_codes.h
    rprim.h
    hdarnold.h
    houdini_logger.h
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
    render_settings.h
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

if (${USD_HAS_HGI_GL})
    target_compile_definitions(render_delegate PUBLIC "USD_HAS_HGI_GL=1")
endif ()

target_link_libraries(render_delegate INTERFACE common)
if (ENABLE_SHARED_ARRAYS)
    target_compile_definitions(render_delegate PUBLIC ENABLE_SHARED_ARRAYS=1)
endif()
if (HYDRA_NORMALIZE_DEPTH)
    target_compile_definitions(render_delegate PUBLIC HYDRA_NORMALIZE_DEPTH=1)
endif()
if (NOT ENABLE_TRACING)
    target_compile_definitions(render_delegate PUBLIC TRACE_ENABLE=0)
endif()

target_compile_definitions(render_delegate PRIVATE "HDARNOLD_EXPORTS=1")
if (BUILD_SCENE_INDEX_PLUGIN OR ENABLE_SCENE_INDEX_IN_BUNDLE)
    target_compile_definitions(render_delegate PUBLIC ENABLE_SCENE_INDEX=1)
endif()
# Whether Hydra 2 is the default traversal mode. Only HdArnoldIsSceneIndexEnabled() in
# utils.cpp reads this, so it is enough to define it for this library.
if (ENABLE_HYDRA2)
    target_compile_definitions(render_delegate PUBLIC ARNOLD_ENABLE_HYDRA2=1)
else()
    target_compile_definitions(render_delegate PUBLIC ARNOLD_ENABLE_HYDRA2=0)
endif()

if (BUILD_HOUDINI_LOGGER)
    # PUBLIC, not PRIVATE: HOUDINI_LOGGER_BUILD gates a member (_houdiniLogger) on
    # HdArnoldRenderDelegate itself (render_delegate.h), so every consumer that
    # constructs/lays out that class - e.g. plugins/render_delegate's hdArnold target,
    # which links render_delegate PRIVATE as a whole archive - must see the same
    # definition render_delegate.cpp was compiled with. A PRIVATE definition here would
    # never reach those consumers (PRIVATE never propagates, even to direct linkers),
    # giving them a mismatched, undersized class layout. Matches the existing
    # ENABLE_SCENE_INDEX / ARNOLD_ENABLE_HYDRA2 PUBLIC definitions above, for the same
    # reason.
    target_compile_definitions(render_delegate PUBLIC HOUDINI_LOGGER_BUILD=1)
    if (HTOA_LOGGER_INCLUDE_DIR)
        target_include_directories(render_delegate PRIVATE "${HTOA_LOGGER_INCLUDE_DIR}")
    endif()
    if (HTOA_LOGGER_LIBRARY)
        target_link_libraries(render_delegate PRIVATE "${HTOA_LOGGER_LIBRARY}")
    endif()
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})
