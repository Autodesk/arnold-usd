set(SRC
    discovery.cpp
    parser.cpp
    utils.cpp
    )

set(HDR
    api.h
    discovery.h
    ndrarnold.h
    parser.h
    utils.h)

add_library(ndrArnold SHARED ${COMMON_SRC} ${SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(ndrArnold PRIVATE ${COMMON_HDR} ${HDR})
endif ()
add_common_dependencies(
    TARGET_NAME ndrArnold
    USD_DEPENDENCIES arch tf gf vt ndr sdr sdf usd)

target_compile_definitions(ndrArnold PRIVATE "NDRARNOLD_EXPORTS=1")

set(PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")

configure_file(
    plugInfo.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
)

install(TARGETS ndrArnold
    DESTINATION "${PREFIX_PLUGINS}")

install(FILES ${PLUGINFO}
    DESTINATION "${PREFIX_PLUGINS}/ndrArnold/resources")

install(FILES ${HDR}
    DESTINATION "${PREFIX_HEADERS}/arnold_usd/ndr")
