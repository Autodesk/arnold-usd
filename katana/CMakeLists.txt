find_package(Katana REQUIRED)
find_package(USDKatana REQUIRED)

set(SRC
    material.cpp
    op.cpp)

set(HDR
    api.h
    material.h)

set(KATANA_API_SOURCES FnAttribute;FnGeolib;FnGeolibServices;FnPluginManager;FnPluginSystem;FnLogging;pystring)

foreach (_api ${KATANA_API_SOURCES})
    file(GLOB_RECURSE _api_src "${KATANA_SOURCE_DIR}/${_api}/*.cpp")
    list(APPEND API_SRC ${_api_src})
endforeach ()

add_library(usdKatanaArnold MODULE ${API_SRC} ${SRC})
target_include_directories(usdKatanaArnold SYSTEM PUBLIC "${USD_INCLUDE_DIR}")
target_include_directories(usdKatanaArnold SYSTEM PUBLIC "${ARNOLD_INCLUDE_DIR}")
target_include_directories(usdKatanaArnold SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
target_include_directories(usdKatanaArnold SYSTEM PUBLIC "${TBB_INCLUDE_DIRS}")
target_include_directories(usdKatanaArnold SYSTEM PUBLIC "${Python2_INCLUDE_DIRS}")
target_include_directories(usdKatanaArnold SYSTEM PUBLIC "${KATANA_INCLUDE_DIR}")
target_include_directories(usdKatanaArnold SYSTEM PUBLIC "${USDKATANA_INCLUDE_DIR}")

target_compile_definitions(usdKatanaArnold PRIVATE "USDKATANAARNOLD_EXPORTS=1")

target_link_libraries(usdKatanaArnold PUBLIC tf gf sdf usd usdGeom usdShade usdKatana vtKatana)

if (WIN32)
    target_compile_definitions(usdKatanaArnold PUBLIC FNPLATFORM_STATIC BOOST_ALL_NO_LIB)
    foreach (_api ${KATANA_API_SOURCES})
        string(TOUPPER ${_api} _api_upper)
        target_compile_definitions(usdKatanaArnold PUBLIC "${_api_upper}_STATIC")
    endforeach ()
else ()
    target_link_libraries(usdKatanaArnold PUBLIC "${KATANA_CEL_LIBRARY}")
endif ()

target_link_libraries(usdKatanaArnold PUBLIC "${ARNOLD_LIBRARY}" "${TBB_LIBRARIES}")
target_link_libraries(usdKatanaArnold PUBLIC "${Boost_LIBRARIES}" Python2::Python)

if (LINUX)
    target_link_libraries(usdKatanaArnold PUBLIC dl)
endif ()

install(TARGETS usdKatanaArnold
        DESTINATION ${PREFIX_KATANA}/Ops)

install(FILES ${HDR}
        DESTINATION ${PREFIX_HEADERS}/katana)
