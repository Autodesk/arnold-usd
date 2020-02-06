set(SRC
        nodes/driver.cpp
        nodes/nodes.cpp

        config.cpp
        constant_strings.cpp
        debug_codes.cpp
        instancer.cpp
        light.cpp
        material.cpp
        mesh.cpp
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
        config.h
        constant_strings.h
        debug_codes.h
        hdarnold.h
        instancer.h
        light.h
        material.h
        mesh.h
        openvdb_asset.h
        points.h
        render_buffer.h
        render_delegate.h
        render_param.h
        render_pass.h
        renderer_plugin.h
        shape.h
        utils.h
        volume.h)

add_library(hdArnold SHARED ${SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(hdArnold PRIVATE ${HDR})
endif ()
target_include_directories(hdArnold SYSTEM PRIVATE "${USD_INCLUDE_DIR}")
target_include_directories(hdArnold SYSTEM PRIVATE "${ARNOLD_INCLUDE_DIR}")
target_include_directories(hdArnold SYSTEM PRIVATE "${Boost_INCLUDE_DIRS}")
target_include_directories(hdArnold SYSTEM PRIVATE "${TBB_INCLUDE_DIRS}")
target_include_directories(hdArnold SYSTEM PRIVATE Python2::Python)
target_include_directories(hdArnold PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(hdArnold PRIVATE "HDARNOLD_EXPORTS=1")

if (${USD_HAS_UPDATED_COMPOSITOR})
    target_compile_definitions(hdArnold PRIVATE "USD_HAS_UPDATED_COMPOSITOR=1")
endif ()

target_link_libraries(hdArnold PRIVATE "${ARNOLD_LIBRARY}" "${TBB_LIBRARIES}")
if (USD_HAS_PYTHON)
    target_link_libraries(hdArnold PRIVATE "${Boost_LIBRARIES}" Python2::Python)
endif ()

if (USD_MONOLITHIC_BUILD)
    target_link_libraries(hdArnold PRIVATE usd_ms)
else ()
    target_link_libraries(hdArnold PRIVATE arch plug tf vt gf work sdf)
    target_link_libraries(hdArnold PRIVATE hf hd hdx usdImaging usdLux pxOsd)
endif ()

if (LINUX)
    target_link_libraries(hdArnold PRIVATE dl)
endif ()

set_target_properties(hdArnold PROPERTIES PREFIX "")

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