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

add_library(hdArnold SHARED ${SRC})
target_include_directories(hdArnold SYSTEM PRIVATE "${USD_INCLUDE_DIR}")
target_include_directories(hdArnold SYSTEM PRIVATE "${ARNOLD_INCLUDE_DIR}")
target_include_directories(hdArnold SYSTEM PRIVATE "${Boost_INCLUDE_DIRS}")
target_include_directories(hdArnold SYSTEM PRIVATE "${TBB_INCLUDE_DIRS}")
target_include_directories(hdArnold SYSTEM PRIVATE "${PYTHON_INCLUDE_DIRS}")
target_include_directories(hdArnold PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(hdArnold PRIVATE "HDARNOLD_EXPORTS=1")

if (USD_1910_UPDATED_COMPOSITOR)
    target_compile_definitions(hdArnold PRIVATE "USD_1910_UPDATED_COMPOSITOR=1")
endif ()

target_link_libraries(hdArnold PRIVATE "${ARNOLD_LIBRARY}" "${PYTHON_LIBRARY}")
target_link_libraries(hdArnold PRIVATE "${Boost_LIBRARIES}" "${TBB_LIBRARIES}")
if (PXR_MONOLITHIC_BUILD)
    target_link_libraries(hdArnold PRIVATE usd_ms)
else ()
    target_link_libraries(hdArnold PRIVATE arch plug tf vt gf work sdf)
    target_link_libraries(hdArnold PRIVATE hf hd hdx usdImaging usdLux pxOsd)
endif ()

if (LINUX)
    target_link_libraries(hdArnold PRIVATE dl)
endif ()

if (BUILD_HOUDINI_TOOLS)
    target_compile_definitions(hdArnold PRIVATE "BUILD_HOUDINI_TOOLS=1")
endif ()

set_target_properties(hdArnold PROPERTIES PREFIX "")

set(PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")

if (LINUX)
    set(LIB_EXTENSION ".so")
elseif (APPLE)
    set(LIB_EXTENSION ".dylib")
else ()
    set(LIB_EXTENSION ".dll")
endif ()

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
        DESTINATION plugin)

install(FILES ${PLUGINFO}
        DESTINATION plugin/hdArnold/resources)
