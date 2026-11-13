# Python module of the interactive render delegate tests, reading back the AOVs of a UsdImagingGL.Engine (see
# interactive_engine_readback.cpp). A test artifact: installed under PREFIX_TESTSUITE_PYTHON, not with the plugins.
set(READBACK_NAME interactive_engine_readback)

if (BUILD_WITH_USD_STATIC OR NOT USD_HAS_PYTHON)
    message(WARNING "BUILD_TESTSUITE_READBACK needs a shared USD with python support, ${READBACK_NAME} isn't built")
    return()
endif ()
# Normally already found by FindUSD or the pxrConfig of USD
if (NOT COMMAND Python3_add_library OR NOT TARGET Python3::Module)
    find_package(Python3 COMPONENTS Development.Module REQUIRED)
endif ()

# A python extension module, named .pyd on Windows. It doesn't link libpython by itself (-undefined dynamic_lookup on
# macOS), but a USD with python support links it already, e.g. through the usd_ms target.
Python3_add_library(${READBACK_NAME} MODULE "${CMAKE_CURRENT_SOURCE_DIR}/interactive_engine_readback.cpp")

target_include_directories(${READBACK_NAME} SYSTEM PRIVATE
    "${USD_INCLUDE_DIR}" ${USD_TRANSITIVE_INCLUDE_DIRS} ${TBB_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
if (USD_MONOLITHIC_BUILD)
    target_link_libraries(${READBACK_NAME} PRIVATE usd_ms)
else ()
    target_link_libraries(${READBACK_NAME} PRIVATE usdImagingGL hd gf tf)
endif ()
target_link_libraries(${READBACK_NAME} PRIVATE ${USD_TRANSITIVE_SHARED_LIBS} ${TBB_LIBRARIES} ${Boost_LIBRARIES})

install(TARGETS ${READBACK_NAME} DESTINATION "${PREFIX_TESTSUITE_PYTHON}")
install_symbols(${READBACK_NAME} "${PREFIX_TESTSUITE_PYTHON}")
