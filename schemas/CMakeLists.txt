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
    ${PYTHON_EXECUTABLE} "${CREATE_SCHEMA_PY}" "${SCHEMAS_OUT}"
)

set(_PYTHONPATHS "")
set(_LIBRARY_PATHS "")
list(APPEND _PYTHONPATHS "$ENV{PYTHONPATH}")
if (USD_LIBRARY_DIR)
    if (EXISTS ${USD_LIBRARY_DIR}/python)
        list(APPEND _PYTHONPATHS "${USD_LIBRARY_DIR}/python")
    endif ()
    list(APPEND _LIBRARY_PATHS "${USD_LIBRARY_DIR}")
endif ()
if (USD_BINARY_DIR)
    list(APPEND _LIBRARY_PATHS "${USD_BINARY_DIR}")
endif ()

if (WIN32)
    list(APPEND _LIBRARY_PATHS "$ENV{PATH}")
    string(JOIN "\;" LIBRARY_PATHS ${_LIBRARY_PATHS})
    string(JOIN "\;" PYTHONPATHS ${_PYTHONPATHS})
else ()
    list(APPEND _LIBRARY_PATHS "$ENV{LD_LIBRARY_PATH}:$ENV{PATH}")
    string(JOIN ":" LIBRARY_PATHS ${_LIBRARY_PATHS})
    string(JOIN ":" PYTHONPATHS ${_PYTHONPATHS})
endif ()

execute_process(
    COMMAND
    ${CMAKE_COMMAND} -E make_directory "${SCHEMAS_SOURCE_OUT}")

# Running schemagen on the schema file and fixing the plugInfo.json.in file in post.
execute_process(
    COMMAND
    ${CMAKE_COMMAND} -E env
    "PYTHONPATH=${PYTHONPATHS}"
    "LD_LIBRARY_PATH=${LIBRARY_PATHS}"
    "PATH=${LIBRARY_PATHS}"
    "${PYTHON_EXECUTABLE}" "${USD_GENSCHEMA}" "${SCHEMAS_USDA}"
    WORKING_DIRECTORY "${SCHEMAS_SOURCE_OUT}"
)

execute_process(
    # UsdGenSchema doesn't setup the library name properly in the file and it's not compatible with
    # configure_file (in older USD versions), so we are manually editing the file using a simple python command.
    COMMAND
    ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/updatePlugInfo.py" "${SCHEMAS_PLUGINFO}"
)

install(FILES "${SCHEMAS_SOURCE_OUT}/generatedSchema.usda"
    DESTINATION "${PREFIX_SCHEMA}/usdArnold/resources")

install(FILES "${SCHEMAS_USDA}"
    DESTINATION "${PREFIX_SCHEMA}/usdArnold/resources/usdArnold")

install(FILES "${SCHEMAS_PLUGINFO}"
    DESTINATION "${PREFIX_SCHEMA}/usdArnold/resources")
