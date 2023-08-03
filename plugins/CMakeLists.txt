if (BUILD_NDR_PLUGIN)
    add_subdirectory(ndr)
endif ()

if (BUILD_USD_IMAGING_PLUGIN)
    add_subdirectory(usd_imaging)
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

