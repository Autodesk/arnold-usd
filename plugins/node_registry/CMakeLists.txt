set(SRC
    discovery.cpp
    parser.cpp
    tokens.cpp
    utils.cpp
    )

set(HDR
    api.h
    discovery.h
    ndrarnold.h
    parser.h
    tokens.h
    utils.h)

# We are building a new "object" target as we need ndr in the procedural as well
add_library(ndrObjects OBJECT ${SRC})
target_compile_definitions(ndrObjects PRIVATE "NDRARNOLD_EXPORTS=1")
add_common_includes(TARGET_NAME ndrObjects DEPENDENCIES common)
if (BUILD_SCENE_INDEX_PLUGIN)
    target_compile_definitions(ndrObjects PUBLIC ENABLE_SCENE_INDEX=1)
endif()

# The NDR plugin must be compiled with USD shared
if (BUILD_NDR_PLUGIN OR ENABLE_HYDRA_IN_USD_PROCEDURAL)
    if (NOT BUILD_WITH_USD_STATIC)
        # Create an USD plugin as shared library
        add_library(nodeRegistryArnold SHARED)
        if (BUILD_HEADERS_AS_SOURCES)
            target_sources(nodeRegistryArnold PRIVATE ${HDR})
        endif ()
        set(NODE_REGISTRY_LIBS arch;tf;gf;vt;sdr;sdf;usd)
        if (${USD_VERSION} VERSION_LESS "0.25.05")
            list(APPEND NODE_REGISTRY_LIBS ndr)
        endif()
        add_common_dependencies(
            TARGET_NAME nodeRegistryArnold
            USD_DEPENDENCIES ${NODE_REGISTRY_LIBS})

        target_link_libraries(nodeRegistryArnold PRIVATE ndrObjects common)
        target_compile_definitions(nodeRegistryArnold PRIVATE "NDRARNOLD_EXPORTS=1")

        # Configure plugInfo.json, it only need the library extension
        set(NDR_PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plug/plugInfo.json")
        install_ndr_arnold_pluginfo(../nodeRegistryArnold "${NDR_PLUGINFO}" "${PREFIX_PLUGINS}")

        # We replicate the layout of the plugin installation inside the build dir for running the testsuite without installing.
        generate_plug_info_for_testsuite(TARGET_NAME nodeRegistryArnold TARGET_PLUGINFO "${NDR_PLUGINFO}")

        install(TARGETS nodeRegistryArnold
            DESTINATION "${PREFIX_PLUGINS}")

        # Why do we intall the headers ??
        #install(FILES ${HDR}
        #    DESTINATION "${PREFIX_HEADERS}/arnold_usd/node_registry")
    endif()
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})

