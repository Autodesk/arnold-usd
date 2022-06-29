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
    mesh.cpp
    native_rprim.cpp
    node_graph.cpp
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
    mesh.h
    native_rprim.h
    node_graph.h
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

set(_usd_deps arch plug tf vt gf work sdf hf hd usdImaging usdLux pxOsd cameraUtil)
if (${USD_VERSION} VERSION_LESS "0.20.5")
    set(_usd_deps ${_usd_deps} hdx)
endif ()
# Enable materialx support in the render delegate.
if (${USD_VERSION} VERSION_GREATER_EQUAL "0.21.8" AND BUILD_ENABLE_MATERIALX_SUPPORT)
    find_package(MaterialX REQUIRED)
    target_include_directories(hdArnold PUBLIC ${MATERIALX_INCLUDE_DIRS})
    set(_usd_deps ${_usd_deps} hdMtlx ndr sdr)
    target_link_libraries(hdArnold PUBLIC ${MATERIALX_LIBRARIES})
    target_compile_definitions(hdArnold PUBLIC "USD_HAS_MATERIALX=1")
    # TODO(pal): We need a new API to get this info from Arnold at runtime.
    target_compile_definitions(hdArnold PUBLIC "ARNOLD_MATERIALX_STDLIB_DIR=\"${ARNOLD_MATERIALX_DIR}\"")
    target_compile_definitions(hdArnold PUBLIC "ARNOLD_MATERIALX_BASE_DIR=\"${ARNOLD_LOCATION}\"")
endif ()

add_common_dependencies(
    TARGET_NAME hdArnold
    USD_DEPENDENCIES ${_usd_deps})
target_compile_definitions(hdArnold PRIVATE "HDARNOLD_EXPORTS=1")

if (${USD_HAS_UPDATED_COMPOSITOR})
    target_compile_definitions(hdArnold PUBLIC "USD_HAS_UPDATED_COMPOSITOR=1")
endif ()

if (${USD_HAS_FULLSCREEN_SHADER})
    target_compile_definitions(hdArnold PUBLIC "USD_HAS_FULLSCREEN_SHADER=1")
endif ()

if (${USD_VERSION} VERSION_GREATER "0.19.7")
    set(RENDERER_PLUGIN_BASE "HdRendererPlugin")
else ()
    set(RENDERER_PLUGIN_BASE "HdxRendererPlugin")
endif ()

# We replicate the layout of the plugin installation inside the build dir for running the testsuite without installing.
# Since we need the same structure for each config, we need to use generator expressions to create the pluginfo
# files inside the build directories. The current workaround for that is to use file(GENERATE).
set(PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plug/plugInfo.json")
configure_file(
    plugInfo.json.in
    "${PLUGINFO}"
)
generate_plug_info_for_testsuite(TARGET_NAME hdArnold TARGET_PLUGINFO "${PLUGINFO}")

install(TARGETS hdArnold
    DESTINATION "${PREFIX_PLUGINS}")

install(FILES "${PLUGINFO}"
    DESTINATION "${PREFIX_PLUGINS}/hdArnold/resources")

install(FILES ${HDR}
    DESTINATION "${PREFIX_HEADERS}/arnold_usd/render_delegate")
