
# We define the plugInfo template here as the procedural is embedding several other usd plugins and it must configure and install the plugInfo.json
set(NDRARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/node_registry/plugInfo.json.in)
set(HDARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/render_delegate/plugInfo.json.in)
set(USDIMAGINGARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/usd_imaging/plugInfo.json.in)
set(SCENEINDEXARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/scene_index/plugInfo.json.in)


add_subdirectory(node_registry)
add_subdirectory(usd_imaging)
add_subdirectory(scene_index)
add_subdirectory(render_delegate)

if (BUILD_PROCEDURAL)
    add_subdirectory(procedural)
endif ()

if (BUILD_BUNDLE)
    add_subdirectory(bundle)
endif()
