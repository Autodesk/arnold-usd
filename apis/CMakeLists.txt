set(SRC
    lightFilterAPI.cpp)

set(WRAP_SRC
    wrapLightFilterAPI.cpp)

add_library(usdArnoldAPI SHARED ${SRC})

target_include_directories(usdArnoldAPI SYSTEM PUBLIC "${USD_INCLUDE_DIR}")
target_include_directories(usdArnoldAPI SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
target_include_directories(usdArnoldAPI SYSTEM PUBLIC "${TBB_INCLUDE_DIRS}")

target_compile_definitions(usdArnoldAPI PRIVATE ARNOLDSCHEMAAPI_EXPORTS)
target_compile_definitions(usdArnoldAPI PUBLIC MFB_PACKAGE_NAME=arnoldSchemaAPI)
target_compile_definitions(usdArnoldAPI PUBLIC MFB_ALT_PACKAGE_NAME=arnoldSchemaAPI)
target_compile_definitions(usdArnoldAPI PUBLIC MFB_PACKAGE_MODULE=arnoldSchemaAPI)

target_link_libraries(usdArnoldAPI PUBLIC "${TBB_LIBRARIES}")
target_link_libraries(usdArnoldAPI PUBLIC "${Boost_LIBRARIES}" Python2::Python)

if (USD_MONOLITHIC_BUILD)
    target_link_libraries(usdArnoldAPI PUBLIC usd_ms)
else ()
    target_link_libraries(usdArnoldAPI PUBLIC arch sdf tf ar vt usd usdGeom usdShade usdLux)
endif ()

add_library(_usdArnoldAPI SHARED ${WRAP_SRC} "${SCHEMAS_WRAP_MODULE}")
set_target_properties(_usdArnoldAPI PROPERTIES PREFIX "")

# We need to use .so on OSX.
if (APPLE)
    set_target_properties(_usdArnoldAPI PROPERTIES SUFFIX ".so")
    # We need to use .pyd on Windows.
elseif (WIN32)
    set_target_properties(_usdArnoldAPI PROPERTIES SUFFIX ".pyd")
endif ()

target_link_libraries(_usdArnoldAPI PRIVATE usdArnoldAPI)

install(TARGETS usdArnoldAPI
    DESTINATION "${PREFIX_LIB}")

install(TARGETS _usdArnoldAPI
    DESTINATION "${PREFIX_LIB}/python/UsdArnoldAPI")

install(FILES __init__.py
    DESTINATION "${PREFIX_LIB}/python/UsdArnoldAPI")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/generatedSchema.usda"
    DESTINATION "${PREFIX_LIB}/usd/usdArnoldAPI/resources")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/schema.usda"
    DESTINATION "${PREFIX_LIB}/usd/usdArnoldAPI/resources/usdArnoldAPI")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json"
    DESTINATION "${PREFIX_LIB}/usd/usdArnoldAPI/resources")
