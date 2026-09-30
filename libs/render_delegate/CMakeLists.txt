set(SRC
    nodes/driver_main.cpp
    nodes/nodes.cpp
    basis_curves.cpp
    camera.cpp
    config.cpp
    coord_sys.cpp
    gaussian_splat.cpp
    houdini/houdini_logger.cpp
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
    houdini/houdini_logger.h
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
    target_compile_definitions(render_delegate PUBLIC HOUDINI_LOGGER_BUILD=1)

    if (NOT TARGET Houdini)
        message(FATAL_ERROR "BUILD_HOUDINI_LOGGER requires building against Houdini (HOUDINI_LOCATION)")
    endif()
    # USD_INCLUDE_DIR already contains the HDK headers, so only the HDK defines are
    # missing. Copy them from the Houdini target rather than linking it, because linking
    # it links every libHoudini* library.
    get_target_property(_hdk_defines Houdini INTERFACE_COMPILE_DEFINITIONS)
    set_property(SOURCE houdini/houdini_logger.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        ${_hdk_defines} "HOUDINI_LOGGER_NAME=\"${HOUDINI_LOGGER_NAME}\"")
    # On macOS, HoudiniConfig links the whole framework and creates no per-library
    # targets.
    if (TARGET Houdini::HoudiniUT)
        target_link_libraries(render_delegate PRIVATE Houdini::HoudiniUT)
    elseif (WIN32)
        # Windows names it libUT and the linker needs the import lib, which lives under
        # custom/houdini/dsolib rather than next to the dll in bin/.
        target_link_libraries(render_delegate PRIVATE
            "${HOUDINI_LOCATION}/custom/houdini/dsolib/libUT${CMAKE_STATIC_LIBRARY_SUFFIX}")
    else()
        target_link_libraries(render_delegate PRIVATE
            "${HOUDINI_LIBS_LOCATION}/libHoudiniUT${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})
