# Add common dependencies to build modules.
# TAGET_NAME - Name of the target.
# USD_DEPENDENCIES - List of individual usd dependencies.
function(add_common_dependencies)
    set(_options "")
    set(_one_value_args TARGET_NAME)
    set(_multi_value_args USD_DEPENDENCIES)

    cmake_parse_arguments(_args "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC "${USD_INCLUDE_DIR}")
    target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC "${ARNOLD_INCLUDE_DIR}")
    target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
    target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC "${TBB_INCLUDE_DIRS}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
    if (BUILD_USE_PYTHON3)
        target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC "${Python3_INCLUDE_DIRS}")
    else ()
        target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC "${Python2_INCLUDE_DIRS}")
    endif ()

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
        target_link_libraries(${_args_TARGET_NAME} PUBLIC usd_ms)
    else ()
        target_link_libraries(${_args_TARGET_NAME} PUBLIC ${_args_USD_DEPENDENCIES})
    endif ()

    if (LINUX)
        target_link_libraries(${_args_TARGET_NAME} PUBLIC dl)
    endif ()

    set_target_properties(${_args_TARGET_NAME} PROPERTIES PREFIX "")
endfunction()