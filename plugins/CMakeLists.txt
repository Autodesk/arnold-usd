
# We define the plugInfo template here as the procedural is embedding several other usd plugins and it must configure and install the plugInfo.json

set(HDARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/render_delegate/plugInfo.json.in)
set(IMAGINGARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/scene_delegate/plugInfo.json.in)
set(USDIMAGINGARNOLD_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/usd_imaging/plugInfo.json.in)

# ndr is deprecated in usd 25.05, we replace it with sdr.
if (${USD_VERSION} VERSION_GREATER_EQUAL "0.25.05")
    set(SDR_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sdr/plugInfo.json.in)
    if (BUILD_SDR_PLUGIN OR ENABLE_HYDRA_IN_USD_PROCEDURAL)
        add_subdirectory(sdr)
    endif ()
else()
    set(NDR_PLUGINFO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/ndr/plugInfo.json.in)
    if (BUILD_NDR_PLUGIN OR ENABLE_HYDRA_IN_USD_PROCEDURAL)
        add_subdirectory(ndr)
    endif ()
endif()

if (BUILD_USD_IMAGING_PLUGIN OR ENABLE_HYDRA_IN_USD_PROCEDURAL)
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

