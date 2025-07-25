
# We define the plugInfo template here as the procedural is embedding several other usd plugins and it must configure and install the plugInfo.json
set(NDRARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/node_registry/plugInfo.json.in)
set(HDARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/render_delegate/plugInfo.json.in)
set(IMAGINGARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/scene_delegate/plugInfo.json.in)
set(USDIMAGINGARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/usd_imaging/plugInfo.json.in)
set(SCENEINDEXARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/scene_index/plugInfo.json.in)

if (BUILD_NDR_PLUGIN OR ENABLE_HYDRA_IN_USD_PROCEDURAL)
    add_subdirectory(node_registry)
endif ()

if (BUILD_USD_IMAGING_PLUGIN OR ENABLE_HYDRA_IN_USD_PROCEDURAL)
    add_subdirectory(usd_imaging)
endif ()

if (BUILD_SCENE_INDEX_PLUGIN)
    add_subdirectory(scene_index)
endif ()

if (BUILD_SCENE_DELEGATE)
    add_subdirectory(scene_delegate)
endif ()

if (BUILD_RENDER_DELEGATE)
    add_subdirectory(render_delegate)
endif ()

if (BUILD_PROCEDURAL)
    add_subdirectory(procedural)
endif ()

