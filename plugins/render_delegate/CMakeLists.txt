if (NOT USD_STATIC_BUILD)

set(SRC
    renderer_plugin.cpp)

add_library(hdArnold SHARED ${COMMON_SRC} ${SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(hdArnold PRIVATE ${COMMON_HDR} ${HDR})
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


set(_usd_deps arch plug trace tf vt gf work sdf hf hd usdImaging usdLux pxOsd cameraUtil)
if (${USD_VERSION} VERSION_LESS "0.20.5")
    set(_usd_deps ${_usd_deps} hdx)
endif ()
add_common_dependencies(
    TARGET_NAME hdArnold
    USD_DEPENDENCIES ${_usd_deps})
target_include_directories(hdArnold PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/render_delegate")
target_compile_definitions(hdArnold PRIVATE "HDARNOLD_EXPORTS=1")
target_link_libraries(hdArnold PRIVATE render_delegate)

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
generate_plug_info_for_testsuite(TARGET_NAME render_delegate TARGET_PLUGINFO "${PLUGINFO}")

install(TARGETS hdArnold
    DESTINATION "${PREFIX_PLUGINS}")

install(FILES "${PLUGINFO}"
    DESTINATION "${PREFIX_PLUGINS}/hdArnold/resources")

# Why do we install the headers ?
#install(FILES ${HDR}
#    DESTINATION "${PREFIX_HEADERS}/arnold_usd/render_delegate")
endif()
