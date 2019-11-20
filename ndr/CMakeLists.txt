set(SRC
        discovery.cpp
        parser.cpp
        utils.cpp
        )

add_library(ndrArnold SHARED ${SRC})
target_include_directories(ndrArnold SYSTEM PRIVATE "${USD_INCLUDE_DIR}")
target_include_directories(ndrArnold SYSTEM PRIVATE "${ARNOLD_INCLUDE_DIR}")
target_include_directories(ndrArnold SYSTEM PRIVATE "${Boost_INCLUDE_DIRS}")
target_include_directories(ndrArnold SYSTEM PRIVATE "${TBB_INCLUDE_DIRS}")
target_include_directories(ndrArnold SYSTEM PRIVATE "${PYTHON_INCLUDE_DIRS}")
target_include_directories(ndrArnold PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(ndrArnold PRIVATE "NDRARNOLD_EXPORTS=1")

target_link_libraries(ndrArnold PRIVATE "${ARNOLD_LIBRARY}" "${PYTHON_LIBRARY}")
target_link_libraries(ndrArnold PRIVATE "${Boost_LIBRARIES}" "${TBB_LIBRARIES}")
if (PXR_MONOLITHIC_BUILD)
    target_link_libraries(ndrArnold PRIVATE usd_ms)
else ()
    target_link_libraries(ndrArnold PRIVATE arch tf gf vt ndr sdr sdf usd)
endif ()

if (LINUX)
    target_link_libraries(ndrArnold PRIVATE dl)
endif ()

set_target_properties(ndrArnold PROPERTIES PREFIX "")

set(PLUGINFO "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")

if (LINUX)
    set(LIB_EXTENSION ".so")
elseif (APPLE)
    set(LIB_EXTENSION ".dylib")
else ()
    set(LIB_EXTENSION ".dll")
endif ()

configure_file(
        plugInfo.json.in
        ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json
)

install(TARGETS ndrArnold
        DESTINATION plugin)

install(FILES ${PLUGINFO}
        DESTINATION plugin/ndrArnold/resources)
