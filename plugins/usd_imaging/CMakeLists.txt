
set(SRC
    arnold_options_adapter.cpp
    material_param_utils.cpp
    node_graph_adapter.cpp
    usd_lux_light_filter_adapter.cpp
    procedural_custom_adapter.cpp
    shape_adapter.cpp
    )

set(HDR
    api.h
    arnold_options_adapter.h
    material_param_utils.h
    node_graph_adapter.h
    usd_lux_light_filter_adapter.h
    shape_adapter.h
    )

# Create the shape adapter file
set(CREATE_ADAPTER_CLASSES "")
set(REGISTER_ADAPTER_CLASSES "")
set(REGISTER_ARNOLD_TYPES "")
foreach (each ${ARNOLD_USDIMAGING_CLASSES})
    set(CREATE_ADAPTER_CLASSES "${CREATE_ADAPTER_CLASSES}\nCREATE_ADAPTER_CLASS(${each});")
    set(REGISTER_ADAPTER_CLASSES "${REGISTER_ADAPTER_CLASSES}\nREGISTER_ADAPTER_CLASS(${each})")
endforeach ()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/shape_adapters.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/shape_adapters.cpp")

# Add objects for the hydra procedural
add_library(usdImagingArnoldObjects OBJECT ${SRC} "${CMAKE_CURRENT_BINARY_DIR}/shape_adapters.cpp")
target_compile_definitions(usdImagingArnoldObjects PRIVATE "USDIMAGINGARNOLD_EXPORTS=1")
add_common_includes(TARGET_NAME usdImagingArnoldObjects DEPENDENCIES common)
if (BUILD_SCENE_INDEX_PLUGIN)
    target_compile_definitions(usdImagingArnoldObjects PUBLIC ENABLE_SCENE_INDEX=1)
endif()

# If we want a usdImagingArnold shared plugin
if (NOT BUILD_WITH_USD_STATIC)

    add_library(usdImagingArnold SHARED ${SRC} "${CMAKE_CURRENT_BINARY_DIR}/shape_adapters.cpp")

    if (BUILD_HEADERS_AS_SOURCES)
        target_sources(usdImagingArnold PRIVATE ${HDR})
    endif ()
    set(USDIMAGING_LIBS ar;arch;plug;tf;trace;vt;gf;work;sdf;sdr;hf;hd;hdsi;usd;usdGeom;usdImaging;usdLux;usdShade)
    if (${USD_VERSION} VERSION_LESS "0.25.05")
        list(APPEND USDIMAGING_LIBS ndr)
    endif()
    add_common_dependencies(
        TARGET_NAME usdImagingArnold
        USD_DEPENDENCIES ${USDIMAGING_LIBS})

    target_link_libraries(usdImagingArnold PRIVATE common)
    target_compile_definitions(usdImagingArnold PRIVATE "USDIMAGINGARNOLD_EXPORTS=1")
    if (BUILD_SCENE_INDEX_PLUGIN)
        target_compile_definitions(usdImagingArnold PUBLIC ENABLE_SCENE_INDEX=1)
    endif()
    # For the generated shape adapters to find headers here.
    target_include_directories(usdImagingArnold PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    
    install(TARGETS usdImagingArnold
        DESTINATION "${PREFIX_PLUGINS}")
    set(PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plug/plugInfo.json")
    install_usdimaging_arnold_pluginfo(
            ../usdImagingArnold
            "${PLUGINFO}"
            ${PREFIX_PLUGINS})

    # We replicate the layout of the plugin installation inside the build dir for running the testsuite without installing.
    generate_plug_info_for_testsuite(TARGET_NAME usdImagingArnold TARGET_PLUGINFO "${PLUGINFO}")

endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR} shape_adapters.cpp.in)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/../common PREFIX common FILES ${COMMON_SRC} ${COMMON_HDR})