set(SRC
    material_param_utils.cpp
    node_graph_adapter.cpp
    usd_lux_light_filter_adapter.cpp
    arnold_mesh_light_adapter.cpp
    procedural_custom_adapter.cpp
    shape_adapter.cpp
    )

set(HDR
    api.h
    material_param_utils.h
    node_graph_adapter.h
    usd_lux_light_filter_adapter.h
    arnold_mesh_light_adapter.h
    shape_adapter.h
    )

# ginstance and instancer is an exception
set(CREATE_ADAPTER_CLASSES "")
set(REGISTER_ADAPTER_CLASSES "")
set(REGISTER_ARNOLD_TYPES "")
foreach (each Alembic Box Cone Curves Disk Implicit Nurbs Plane Points Polymesh Procedural Sphere Usd Volume VolumeImplicit)
    set(CREATE_ADAPTER_CLASSES "${CREATE_ADAPTER_CLASSES}\nCREATE_ADAPTER_CLASS(${each});")
    set(REGISTER_ADAPTER_CLASSES "${REGISTER_ADAPTER_CLASSES}\nREGISTER_ADAPTER_CLASS(${each})")
    set(REGISTER_ARNOLD_TYPES "${REGISTER_ARNOLD_TYPES}\n\"UsdImagingArnold${each}Adapter\":{\"bases\":[\"UsdImagingGprimAdapter\"],\"primTypeName\":\"Arnold${each}\"},")
endforeach ()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/shape_adapters.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/shape_adapters.cpp")

add_library(usdImagingArnold SHARED ${COMMON_SRC} ${SRC} "${CMAKE_CURRENT_BINARY_DIR}/shape_adapters.cpp")
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(usdImagingArnold PRIVATE ${COMMON_HDR} ${HDR})
endif ()
add_common_dependencies(
    TARGET_NAME usdImagingArnold
    USD_DEPENDENCIES ar arch plug tf trace vt gf work ndr sdf sdr
                     hf hd hdx usd usdGeom usdImaging usdLux usdShade)
target_compile_definitions(usdImagingArnold PRIVATE "USDIMAGINGARNOLD_EXPORTS=1")
# For the generated shape adapters to find headers here.
target_include_directories(usdImagingArnold PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR} shape_adapters.cpp.in)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/../common PREFIX common FILES ${COMMON_SRC} ${COMMON_HDR})

set(PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plug/plugInfo.json")
configure_file(
    plugInfo.json.in
    "${PLUGINFO}"
)

# We replicate the layout of the plugin installation inside the build dir for running the testsuite without installing.
generate_plug_info_for_testsuite(TARGET_NAME usdImagingArnold TARGET_PLUGINFO "${PLUGINFO}")

install(TARGETS usdImagingArnold
    DESTINATION "${PREFIX_PLUGINS}")

install(FILES "${PLUGINFO}"
    DESTINATION "${PREFIX_PLUGINS}/usdImagingArnold/resources")

install(FILES ${HDR}
    DESTINATION "${PREFIX_HEADERS}/arnold_usd/usd_imaging")
