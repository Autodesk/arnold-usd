set(SRC
    usd_lux_light_filter_adapter.cpp
    shape_adapter.cpp
    )

set(HDR
    api.h
    usd_lux_light_filter_adapter.h
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
    USD_DEPENDENCIES arch plug tf vt gf work sdf
                     hf hd hdx usd usdGeom usdImaging usdLux usdShade)
target_compile_definitions(usdImagingArnold PRIVATE "USDIMAGINGARNOLD_EXPORTS=1")
# For the generated shape adapters to find headers here.
target_include_directories(usdImagingArnold PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set(PLUGINFO_OUT "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")

configure_file(
    plugInfo.json.in
    "${PLUGINFO_OUT}"
)

install(TARGETS usdImagingArnold
    DESTINATION "${PREFIX_PLUGINS}")

install(FILES ${PLUGINFO_OUT}
    DESTINATION "${PREFIX_PLUGINS}/usdImagingArnold/resources")

install(FILES ${HDR}
    DESTINATION "${PREFIX_HEADERS}/arnold_usd/usd_imaging")
