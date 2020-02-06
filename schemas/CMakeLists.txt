set(SCHEMAS_OUT "${CMAKE_CURRENT_BINARY_DIR}/${ARNOLD_VERSION}/${USD_VERSION}")
set(SCHEMAS_SOURCE_OUT "${SCHEMAS_OUT}/source")

set(SCHEMAS_TOKENS "${SCHEMAS_SOURCE_OUT}/tokens.cpp")
set(SCHEMAS_PLUGINFO "${SCHEMAS_SOURCE_OUT}/plugInfo.json")
set(SCHEMAS_WRAP_MODULE "${SCHEMAS_OUT}/wrapModule.cpp")
set(SCHEMAS_USDA "${SCHEMAS_OUT}/schema.usda")
set(CREATE_SCHEMA_PY "${CMAKE_CURRENT_SOURCE_DIR}/createSchemaFile.py")

# Using add_custom_command for the schemagen won't work, as it runs after any file(GLOB) or include() calls, and we
# need a list of source files for the build.

# Generating the schema file.
execute_process(
        COMMAND
        ${CMAKE_COMMAND} -E env
        "PYTHONPATH=${ARNOLD_PYTHON_DIR}"
        "LD_LIBRARY_PATH=${ARNOLD_BINARY_DIR}"
        ${Python2_EXECUTABLE} "${CREATE_SCHEMA_PY}" "${SCHEMAS_OUT}"
)

# Running schemagen on the schema file and fixing the plugInfo.json file in post.
execute_process(
        COMMAND
        ${CMAKE_COMMAND} -E make_directory "${SCHEMAS_SOURCE_OUT}"
        COMMAND
        ${CMAKE_COMMAND} -E env
        "PYTHONPATH=${USD_LIBRARY_DIR}/python"
        "LD_LIBRARY_PATH=${USD_LIBRARY_DIR}"
        ${USD_GENSCHEMA} "${SCHEMAS_USDA}" "${SCHEMAS_SOURCE_OUT}"
        # UsdGenSchema doesn't setup the library name properly in the file and it's not compatible with
        # configure_file, so we are manually editing the file using a simple python command.
        COMMAND
        ${Python2_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/updatePlugInfo.py" "${SCHEMAS_PLUGINFO}"
)

file(GLOB SRC CONFIGURE_DEPENDS "${SCHEMAS_SOURCE_OUT}/arnold*.cpp")
file(GLOB WRAP_SRC CONFIGURE_DEPENDS "${SCHEMAS_SOURCE_OUT}/wrap*.cpp")
file(GLOB HDR CONFIGURE_DEPENDS "${SCHEMAS_SOURCE_OUT}/*.h")

add_library(usdArnold SHARED "${SCHEMAS_TOKENS}" ${SRC} "${SCHEMAS_SOURCE_OUT}/tokens.cpp" moduleDeps.cpp)

target_include_directories(usdArnold SYSTEM PUBLIC "${USD_INCLUDE_DIR}")
target_include_directories(usdArnold SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
target_include_directories(usdArnold SYSTEM PUBLIC "${TBB_INCLUDE_DIRS}")

target_compile_definitions(usdArnold PRIVATE USDARNOLD_EXPORTS)
target_compile_definitions(usdArnold PUBLIC MFB_PACKAGE_NAME=usdArnold)
target_compile_definitions(usdArnold PUBLIC MFB_ALT_PACKAGE_NAME=usdArnold)
target_compile_definitions(usdArnold PUBLIC MFB_PACKAGE_MODULE=UsdArnold)

target_link_libraries(usdArnold PUBLIC "${TBB_LIBRARIES}")
target_link_libraries(usdArnold PUBLIC "${Boost_LIBRARIES}" Python2::Python)

if (USD_MONOLITHIC_BUILD)
    target_link_libraries(usdArnold PUBLIC usd_ms)
else ()
    target_link_libraries(usdArnold PUBLIC arch sdf tf usd ar usdGeom usdShade vt usdLux)
endif ()

add_library(_usdArnold SHARED ${WRAP_SRC} "${SCHEMAS_WRAP_MODULE}")
set_target_properties(_usdArnold PROPERTIES PREFIX "")

# We need to use .so on OSX.
if (APPLE)
    set_target_properties(_usdArnold PROPERTIES SUFFIX ".so")
# We need to use .pyd on Windows.
elseif (WIN32)
    set_target_properties(_usdArnold PROPERTIES SUFFIX ".pyd")
endif ()

target_link_libraries(_usdArnold PRIVATE usdArnold)

install(TARGETS usdArnold
        DESTINATION "${PREFIX_LIB}")

install(TARGETS _usdArnold
        DESTINATION "${PREFIX_LIB}/python/UsdArnold")

install(FILES __init__.py
        DESTINATION "${PREFIX_LIB}/python/UsdArnold")

install(FILES "${SCHEMAS_SOURCE_OUT}/generatedSchema.usda"
        DESTINATION "${PREFIX_LIB}/usd/usdArnold/resources")

install(FILES "${SCHEMAS_USDA}"
        DESTINATION "${PREFIX_LIB}/usd/usdArnold/resources/usdArnold")

install(FILES "${SCHEMAS_PLUGINFO}"
        DESTINATION "${PREFIX_LIB}/usd/usdArnold/resources")
