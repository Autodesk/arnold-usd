# Add the includes shared by all the modules
function(add_common_includes)
    set(_options "")
    set(_one_value_args TARGET_NAME)
    set(_multi_value_args )

    cmake_parse_arguments(_args "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    target_include_directories(${_args_TARGET_NAME} PUBLIC "${USD_INCLUDE_DIR}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${ARNOLD_INCLUDE_DIR}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${Boost_INCLUDE_DIRS}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${TBB_INCLUDE_DIRS}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${PROJECT_SOURCE_DIR}/libs/common")

    if (BUILD_USE_PYTHON3)
        target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC "${Python3_INCLUDE_DIRS}")
    else ()
        target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC "${Python2_INCLUDE_DIRS}")
    endif ()

endfunction()


# Add common dependencies to build modules.
# TAGET_NAME - Name of the target.
# USD_DEPENDENCIES - List of individual usd dependencies.
function(add_common_dependencies)
    set(_options "")
    set(_one_value_args TARGET_NAME)
    set(_multi_value_args USD_DEPENDENCIES)

    cmake_parse_arguments(_args "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    add_common_includes(TARGET_NAME ${_args_TARGET_NAME})
    target_link_libraries(${_args_TARGET_NAME} PUBLIC "${ARNOLD_LIBRARY}" "${TBB_LIBRARIES}")
    if (USD_HAS_PYTHON)
        target_link_libraries(${_args_TARGET_NAME} PUBLIC "${Boost_LIBRARIES}")
        if (BUILD_USE_PYTHON3)
            target_link_libraries(${_args_TARGET_NAME} PUBLIC Python3::Python)
        else ()
            target_link_libraries(${_args_TARGET_NAME} PUBLIC Python2::Python)
        endif ()
    endif ()

    if (USD_MONOLITHIC_BUILD)
        # usd_ms is the shared library version. usd_m is the static one.
        target_link_libraries(${_args_TARGET_NAME} PUBLIC $<IF:$<BOOL:${BUILD_WITH_USD_STATIC}>,usd_m,usd_ms> ${USD_TRANSITIVE_SHARED_LIBS} ${USD_TRANSITIVE_STATIC_LIBS})
    else ()
        target_link_libraries(${_args_TARGET_NAME} PUBLIC ${_args_USD_DEPENDENCIES} ${USD_TRANSITIVE_SHARED_LIBS} ${USD_TRANSITIVE_STATIC_LIBS})
    endif ()

    if (LINUX)
        target_link_libraries(${_args_TARGET_NAME} PUBLIC dl)
    endif ()

    set_target_properties(${_args_TARGET_NAME} PROPERTIES PREFIX "")
endfunction()

function(generate_plug_info_for_testsuite)
    set(_options "")
    set(_one_value_args TARGET_NAME TARGET_PLUGINFO)
    set(_multi_value_args "")

    cmake_parse_arguments(_args "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    file(READ "${_args_TARGET_PLUGINFO}" _plug)
    file(
        GENERATE OUTPUT "$<TARGET_FILE_DIR:${_args_TARGET_NAME}>/${_args_TARGET_NAME}/resources/plugInfo.json"
        CONTENT "${_plug}"
        FILE_PERMISSIONS OWNER_READ
        NEWLINE_STYLE UNIX
    )
    file(READ "${CMAKE_SOURCE_DIR}/plugInfo.json" _main_plug)
    file(
        GENERATE OUTPUT "$<TARGET_FILE_DIR:${_args_TARGET_NAME}>/plugInfo.json"
        CONTENT "${_main_plug}"
        FILE_PERMISSIONS OWNER_READ
        NEWLINE_STYLE UNIX
    )
endfunction()
