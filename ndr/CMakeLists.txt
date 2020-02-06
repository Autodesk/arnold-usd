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

add_library(ndrArnold SHARED ${SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(ndrArnold PRIVATE ${HDR})
endif ()
target_include_directories(ndrArnold SYSTEM PRIVATE "${USD_INCLUDE_DIR}")
target_include_directories(ndrArnold SYSTEM PRIVATE "${ARNOLD_INCLUDE_DIR}")
target_include_directories(ndrArnold SYSTEM PRIVATE "${Boost_INCLUDE_DIRS}")
target_include_directories(ndrArnold SYSTEM PRIVATE "${TBB_INCLUDE_DIRS}")
target_include_directories(ndrArnold SYSTEM PRIVATE Python2::Python)
target_include_directories(ndrArnold PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(ndrArnold PRIVATE "NDRARNOLD_EXPORTS=1")

target_link_libraries(ndrArnold PRIVATE "${ARNOLD_LIBRARY}" "${TBB_LIBRARIES}")
if (USD_HAS_PYTHON)
    target_link_libraries(ndrArnold PRIVATE "${Boost_LIBRARIES}" Python2::Python)
endif ()

if (USD_MONOLITHIC_BUILD)
    target_link_libraries(ndrArnold PRIVATE usd_ms)
else ()
    target_link_libraries(ndrArnold PRIVATE arch tf gf vt ndr sdr sdf usd)
endif ()

if (LINUX)
    target_link_libraries(ndrArnold PRIVATE dl)
endif ()

set_target_properties(ndrArnold PROPERTIES PREFIX "")

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
        DESTINATION "${PREFIX_HEADERS}/ndr")
