set(SCHEMAS_OUT "${CMAKE_CURRENT_BINARY_DIR}/${ARNOLD_VERSION}/${USD_VERSION}")
set(SCHEMAS_SOURCE_OUT "${SCHEMAS_OUT}/source")

set(SCHEMAS_TOKENS "${SCHEMAS_SOURCE_OUT}/tokens.cpp")
set(SCHEMAS_PLUGINFO "${SCHEMAS_SOURCE_OUT}/plugInfo.json")
set(SCHEMAS_WRAP_MODULE "${SCHEMAS_OUT}/wrapModule.cpp")
set(SCHEMAS_USDA "${SCHEMAS_OUT}/schema.usda")
set(CREATE_SCHEMA_PY "${CMAKE_CURRENT_SOURCE_DIR}/createSchemaFile.py")
set(RUN_WITH_TIMEOUT_PY "${CMAKE_CURRENT_SOURCE_DIR}/run_with_timeout.py")

# hython's license daemon (hserver, on Windows) can be lazily spawned on the first
# license touch and then runs forever as a machine-wide singleton; it inherits its
# parent's working directory, which would otherwise pin whichever step touches a
# license first to a directory inside the build tree -- permanently blocking
# `abuild --clean`. Anchor it to hython's own install directory instead.
get_filename_component(_SCHEMA_GEN_SAFE_CWD "${PYTHON_EXECUTABLE}" DIRECTORY)
if (NOT _SCHEMA_GEN_SAFE_CWD)
    set(_SCHEMA_GEN_SAFE_CWD "${CMAKE_CURRENT_BINARY_DIR}")
endif ()

set(_PYTHONPATHS "${ARNOLD_PYTHON_DIR}")
set(_LIBRARY_PATHS "${ARNOLD_BINARY_DIR}")
list(APPEND _PYTHONPATHS "$ENV{PYTHONPATH}")
if (USD_LIBRARY_DIR)
    if (EXISTS ${USD_LIBRARY_DIR}/python)
        list(APPEND _PYTHONPATHS "${USD_LIBRARY_DIR}/python")
    endif ()
    list(APPEND _LIBRARY_PATHS "${USD_LIBRARY_DIR}")
    list(APPEND _LIBRARY_PATHS "${USD_LOCATION}/bin")
endif ()
if (USD_BINARY_DIR)
    list(APPEND _LIBRARY_PATHS "${USD_BINARY_DIR}")
endif ()

if (WIN32)
    list(APPEND _LIBRARY_PATHS "$ENV{PATH}")
    cmake_path(CONVERT "${_LIBRARY_PATHS}" TO_NATIVE_PATH_LIST _LIBRARY_PATHS_NATIVE NORMALIZE)
    string(JOIN "\;" LIBRARY_PATHS ${_LIBRARY_PATHS_NATIVE})
    cmake_path(CONVERT "${_PYTHONPATHS}" TO_NATIVE_PATH_LIST _PYTHONPATHS NORMALIZE)
    string(JOIN "\;" PYTHONPATHS ${_PYTHONPATHS})
else ()
    list(APPEND _LIBRARY_PATHS "$ENV{LD_LIBRARY_PATH}:$ENV{PATH}")
    string(JOIN ":" LIBRARY_PATHS ${_LIBRARY_PATHS})
    string(JOIN ":" PYTHONPATHS ${_PYTHONPATHS})
endif ()

# Windows has no notion of LD_LIBRARY_PATH -- DLL search uses PATH -- so setting it there is a
# no-op that only serves to duplicate the (often huge, tool-laden) PATH a second time on the
# generated command line. That regularly pushes these lines past cmd.exe's ~8191-character
# limit, which silently corrupts the trailing "|| (set FAIL_LINE=...)" error handler and gets
# swallowed into the preceding command's argv (surfaces as e.g. usdGenSchema: error:
# unrecognized arguments: (set FAIL_LINE=3& goto :ABORT)).
if (WIN32)
    set(_LD_LIBRARY_PATH_ENV)
else ()
    set(_LD_LIBRARY_PATH_ENV "LD_LIBRARY_PATH=${LIBRARY_PATHS}")
endif ()

if (NOT USD_GENSCHEMA AND NOT BUILD_USDGENSCHEMA_ARNOLD)
    message(WARNING "usdGenSchema was not found, the schemas won't be generated with it. Consider using the internal usdgenschema by setting BUILD_USDGENSCHEMA_ARNOLD=True")
endif()

# Custom command for generating an arnold schema from the arnold api
add_custom_command(OUTPUT ${SCHEMAS_USDA}
    COMMAND
    ${CMAKE_COMMAND} -E env
    "PYTHONPATH=${PYTHONPATHS}"
    ${_LD_LIBRARY_PATH_ENV}
    "PATH=${LIBRARY_PATHS}"
    ${PYTHON_EXECUTABLE} "${RUN_WITH_TIMEOUT_PY}" --timeout ${SCHEMA_GEN_TIMEOUT}
    --cleanup "${SCHEMAS_USDA}" --cleanup "${SCHEMAS_WRAP_MODULE}"
    -- ${PYTHON_EXECUTABLE} "${CREATE_SCHEMA_PY}" "${SCHEMAS_OUT}"
    WORKING_DIRECTORY "${_SCHEMA_GEN_SAFE_CWD}"
    DEPENDS "${CREATE_SCHEMA_PY}" "${RUN_WITH_TIMEOUT_PY}"
)

# Custom target for generating an arnold schema from the arnold api
add_custom_target(schemaUsda
    DEPENDS ${SCHEMAS_USDA}
)

add_custom_target(schemaSourceOut
    BYPRODUCTS "${SCHEMAS_SOURCE_OUT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SCHEMAS_SOURCE_OUT}"
)

if (BUILD_USDGENSCHEMA_ARNOLD)
    # Running our custom usdGenSchemaArnold on the schema file
    add_custom_command(OUTPUT "${SCHEMAS_SOURCE_OUT}/generatedSchema.usda"
        COMMAND
            ${CMAKE_COMMAND} -E env "$<TARGET_FILE:usdGenSchemaArnold>" "${SCHEMAS_USDA}" "${SCHEMAS_SOURCE_OUT}"
        WORKING_DIRECTORY ${SCHEMAS_SOURCE_OUT}
        DEPENDS schemaSourceOut schemaUsda usdGenSchemaArnold $<$<TARGET_EXISTS:usd_proc>:usd_proc>
    )
else()
    # Running usdGenSchema on the schema file
    add_custom_command(OUTPUT "${SCHEMAS_SOURCE_OUT}/generatedSchema.usda"
        COMMAND
            ${CMAKE_COMMAND} -E env
            "PYTHONPATH=${PYTHONPATHS}"
            ${_LD_LIBRARY_PATH_ENV}
            "PATH=${LIBRARY_PATHS}"
            "${PYTHON_EXECUTABLE}" "${RUN_WITH_TIMEOUT_PY}" --timeout ${SCHEMA_GEN_TIMEOUT}
            --cleanup "${SCHEMAS_SOURCE_OUT}/generatedSchema.usda"
            -- "${PYTHON_EXECUTABLE}" "${USD_GENSCHEMA}" "${SCHEMAS_USDA}"
        WORKING_DIRECTORY ${SCHEMAS_SOURCE_OUT}
        DEPENDS schemaSourceOut schemaUsda "${RUN_WITH_TIMEOUT_PY}" $<$<TARGET_EXISTS:usd_proc>:usd_proc>
    )
endif()

add_custom_target(generatedSchema
    DEPENDS "${SCHEMAS_SOURCE_OUT}/generatedSchema.usda"
)

# Fixing the plugInfo.json.in file in post.
add_custom_target(updatedPlugInfo
    BYPRODUCTS "${SCHEMAS_PLUGINFO}"
    # UsdGenSchema doesn't setup the library name properly in the file and it's not compatible with
    # configure_file (in older USD versions), so we are manually editing the file using a simple python command.
    COMMAND
        ${PYTHON_EXECUTABLE} "${RUN_WITH_TIMEOUT_PY}" --timeout ${SCHEMA_GEN_TIMEOUT}
        --cleanup "${SCHEMAS_PLUGINFO}"
        -- ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/updatePlugInfo.py" "${SCHEMAS_PLUGINFO}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/tools"
    DEPENDS generatedSchema "${RUN_WITH_TIMEOUT_PY}"
)

# The final target is named schemas, it has the plugInfo updated and the generated schemas
# It is always built if this file was included.
add_custom_target(schemas ALL
    DEPENDS generatedSchema updatedPlugInfo
)

install(FILES "${SCHEMAS_SOURCE_OUT}/generatedSchema.usda"
    DESTINATION "${PREFIX_SCHEMA}/usdArnold/resources")

install(FILES "${SCHEMAS_SOURCE_OUT}/generatedSchema.usda"
    DESTINATION "${PREFIX_SCHEMA}/usdArnold/resources/usdArnold")

install(FILES "${SCHEMAS_USDA}"
    DESTINATION "${PREFIX_SCHEMA}/usdArnold/resources/usdArnold")

install(FILES "${SCHEMAS_PLUGINFO}"
    DESTINATION "${PREFIX_SCHEMA}/usdArnold/resources")

if (BUILD_PROCEDURAL AND BUILD_WITH_USD_STATIC)
    install(FILES "${SCHEMAS_PLUGINFO}"
        DESTINATION "${PREFIX_PROCEDURAL}/usd/usdArnold/resources")
    install(FILES "${SCHEMAS_SOURCE_OUT}/generatedSchema.usda"
        DESTINATION "${PREFIX_PROCEDURAL}/usd/usdArnold/resources")
endif()
