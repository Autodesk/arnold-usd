set(SRC
    discovery.cpp
    parser.cpp
    tokens.cpp
    utils.cpp
    )

set(HDR
    api.h
    discovery.h
    sdrarnold.h
    parser.h
    tokens.h
    utils.h)

# We are building a new "object" target as we need sdr in the procedural as well
add_library(sdrObjects OBJECT ${SRC})
target_compile_definitions(sdrObjects PRIVATE "SDRARNOLD_EXPORTS=1")
add_common_includes(TARGET_NAME sdrObjects DEPENDENCIES common)

# The SDR plugin must be compiled with USD shared
if (NOT BUILD_WITH_USD_STATIC)
    # Create an USD plugin as shared library
    add_library(sdrArnold SHARED)
    if (BUILD_HEADERS_AS_SOURCES)
        target_sources(sdrArnold PRIVATE ${HDR})
    endif ()
    add_common_dependencies(
        TARGET_NAME sdrArnold
        USD_DEPENDENCIES arch tf gf vt sdr sdr sdf usd)

    target_link_libraries(sdrArnold PRIVATE sdrObjects common)
    target_compile_definitions(sdrArnold PRIVATE "SDRARNOLD_EXPORTS=1")

    # Configure plugInfo.json, it only need the library extension
    set(SDR_PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plug/plugInfo.json")
    install_sdr_arnold_pluginfo(../sdrArnold "${SDR_PLUGINFO}" "${PREFIX_PLUGINS}")

    # We replicate the layout of the plugin installation inside the build dir for running the testsuite without installing.
    generate_plug_info_for_testsuite(TARGET_NAME sdrArnold TARGET_PLUGINFO "${SDR_PLUGINFO}")

    install(TARGETS sdrArnold
        DESTINATION "${PREFIX_PLUGINS}")

    # Why do we intall the headers ??
    #install(FILES ${HDR}
    #    DESTINATION "${PREFIX_HEADERS}/arnold_usd/sdr")
endif()


source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})

