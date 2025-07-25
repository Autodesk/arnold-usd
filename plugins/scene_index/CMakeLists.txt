set(SRC
    dependenciesSIP.cpp
    dependencyForwardingSIP.cpp
    extComputationPrimvarPruningSIP.cpp
    fixInvalidationSIP.cpp
    fixMaterialPruningSIP.cpp
    fixPropagatedPrototypesVisibilitySIP.cpp
    implicitSurfaceSIP.cpp
    lightLinkingSIP.cpp
    nurbsApproximatingSIP.cpp
)

set(HDR
    dependenciesSIP.h
    dependencyForwardingSIP.h
    extComputationPrimvarPruningSIP.h
    fixInvalidationSIP.h
    fixMaterialPruningSIP.h
    fixPropagatedPrototypesVisibilitySIP.h
    implicitSurfaceSIP.h
    nurbsApproximatingSIP.h
)

# Add objects for the hydra procedural
add_library(sceneIndexArnoldObjects OBJECT ${SRC})
target_compile_definitions(sceneIndexArnoldObjects PRIVATE "HDSCENEINDEX_EXPORTS=1")
add_common_includes(TARGET_NAME sceneIndexArnoldObjects DEPENDENCIES common)
if (BUILD_SCENE_INDEX_PLUGIN)
    target_compile_definitions(sceneIndexArnoldObjects PUBLIC ENABLE_SCENE_INDEX=1)
endif()

# If we want a sceneIndexArnold shared plugin
if (NOT BUILD_WITH_USD_STATIC)

    add_library(sceneIndexArnold SHARED ${SRC})

    if (BUILD_HEADERS_AS_SOURCES)
        target_sources(sceneIndexArnold PRIVATE ${HDR})
    endif ()
    #TODO check only the useful ones
    set(SCENEINDEX_LIBS ar;arch;plug;tf;trace;vt;gf;work;sdf;sdr;hf;hd;usd;usdGeom;usdImaging;hdsi;usdLux;usdShade;usdSkelImaging)
    if (${USD_VERSION} VERSION_LESS "0.25.05")
        list(APPEND SCENEINDEX_LIBS ndr)
    endif()
    add_common_dependencies(
        TARGET_NAME sceneIndexArnold
        USD_DEPENDENCIES ${SCENEINDEX_LIBS})

    target_link_libraries(sceneIndexArnold PRIVATE common)
    target_compile_definitions(sceneIndexArnold PRIVATE "HDSCENEINDEXARNOLD_EXPORTS=1")
    # For the generated shape adapters to find headers here.
    target_include_directories(sceneIndexArnold PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    
    install(TARGETS sceneIndexArnold
        DESTINATION "${PREFIX_PLUGINS}")
    set(PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plug/plugInfo.json")
    install_sceneIndex_arnold_pluginfo(
            ../sceneIndexArnold
            "${PLUGINFO}"
            ${PREFIX_PLUGINS})

    # We replicate the layout of the plugin installation inside the build dir for running the testsuite without installing.
    generate_plug_info_for_testsuite(TARGET_NAME sceneIndexArnold TARGET_PLUGINFO "${PLUGINFO}")

endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/../common PREFIX common FILES ${COMMON_SRC} ${COMMON_HDR})