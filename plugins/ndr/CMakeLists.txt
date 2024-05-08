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

# The NDR plugin must be compiled with USD shared
if (NOT BUILD_WITH_USD_STATIC)
    # Create an USD plugin as shared library
    add_library(ndrArnold SHARED)
    if (BUILD_HEADERS_AS_SOURCES)
        target_sources(ndrArnold PRIVATE ${HDR})
    endif ()
    add_common_dependencies(
        TARGET_NAME ndrArnold
        USD_DEPENDENCIES arch tf gf vt ndr sdr sdf usd)

    target_link_libraries(ndrArnold PRIVATE ndrObjects common)
    target_compile_definitions(ndrArnold PRIVATE "NDRARNOLD_EXPORTS=1")

    # Configure plugInfo.json, it only need the library extension
    install_ndr_arnold_pluginfo(../ndrArnold "${CMAKE_CURRENT_BINARY_DIR}/plug/plugInfo.json" "${PREFIX_PLUGINS}")

    # We replicate the layout of the plugin installation inside the build dir for running the testsuite without installing.
    generate_plug_info_for_testsuite(TARGET_NAME ndrArnold TARGET_PLUGINFO "${NDR_PLUGINFO}")

    install(TARGETS ndrArnold
        DESTINATION "${PREFIX_PLUGINS}")

    # Why do we intall the headers ??
    #install(FILES ${HDR}
    #    DESTINATION "${PREFIX_HEADERS}/arnold_usd/ndr")
endif()


source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})

