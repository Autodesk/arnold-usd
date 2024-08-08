# Add the includes shared by all the modules
# TARGET_NAME - Name of the target.
# DEPENDENCIES - List of individual dependencies
function(add_common_includes)
    set(_options "")
    set(_one_value_args TARGET_NAME)
    set(_multi_value_args DEPENDENCIES)

    cmake_parse_arguments(_args "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    target_include_directories(${_args_TARGET_NAME} PUBLIC "${USD_INCLUDE_DIR}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${USD_TRANSITIVE_INCLUDE_DIRS}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${ARNOLD_INCLUDE_DIR}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${Boost_INCLUDE_DIRS}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${TBB_INCLUDE_DIRS}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(${_args_TARGET_NAME} PUBLIC "${PROJECT_SOURCE_DIR}/libs/common")

    # We include whatever python include was found previously
    target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC ${Python2_INCLUDE_DIRS})
    target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC ${Python3_INCLUDE_DIRS})
    target_include_directories(${_args_TARGET_NAME} SYSTEM PUBLIC ${Python_INCLUDE_DIRS})

    # We look for the includes exported by the dependencies
    foreach(DEP_ ${_args_DEPENDENCIES})
        if (TARGET ${DEP_})
            get_property(INC_ TARGET ${DEP_} PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
            target_include_directories(${_args_TARGET_NAME} PUBLIC ${INC_})
            get_property(INC_SYS TARGET ${DEP_} PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
            target_include_directories(${_args_TARGET_NAME} PUBLIC ${INC_SYS})
        endif()
    endforeach()

endfunction()


# Add common dependencies to build modules.
# TAGET_NAME - Name of the target.
# USD_DEPENDENCIES - List of individual usd dependencies.
function(add_common_dependencies)
    set(_options "")
    set(_one_value_args TARGET_NAME)
    set(_multi_value_args USD_DEPENDENCIES)

    cmake_parse_arguments(_args "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    if (USD_MONOLITHIC_BUILD)
        add_common_includes(TARGET_NAME ${_args_TARGET_NAME} DEPENDENCIES usd_ms usd_m)
    else()
        add_common_includes(TARGET_NAME ${_args_TARGET_NAME} DEPENDENCIES ${_args_USD_DEPENDENCIES})
    endif()


    target_link_libraries(${_args_TARGET_NAME} PUBLIC "${ARNOLD_LIBRARY}" "${TBB_LIBRARIES}" "${Boost_LIBRARIES}")
    # This should be USD_NEEDS_PYTHON instead of USD_HAS_PYTHON in case it's not in its dependencies
    # and USD_NEEDS_PYTHON should be set only if python wasn't found in the pxrConfig and usd uses python
    # Leaving this code commented while testing on multiple platform and situation
    # if (USD_HAS_PYTHON)
    #     target_link_libraries(${_args_TARGET_NAME} PUBLIC "${Boost_LIBRARIES}")
    #     if (BUILD_USE_PYTHON3)
    #         target_link_libraries(${_args_TARGET_NAME} PUBLIC Python3::Python)
    #     else ()
    #         target_link_libraries(${_args_TARGET_NAME} PUBLIC Python2::Python)
    #     endif ()
    # endif ()

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

# Depending on the configuration we will have to install the ndr pluginfo with a different content and
# at a different location, so we use this function to do so
function(install_ndr_arnold_pluginfo LIB_PATH NDR_PLUGINFO CONFIG_ROOT)
    # LIB_PATH is used in the plugInfo.json.in, do not rename
    set(LIB_EXTENSION ${CMAKE_SHARED_LIBRARY_SUFFIX})
    configure_file(
        "${NDR_PLUGINFO_SRC}"
        "${NDR_PLUGINFO}"
    )
    install(FILES "${NDR_PLUGINFO}"
        DESTINATION "${CONFIG_ROOT}/ndrArnold/resources")
endfunction()

set(ARNOLD_USDIMAGING_CLASSES Alembic Box Cone Curves Disk Ginstance Implicit Nurbs Plane Points Polymesh Procedural Sphere Usd Volume VolumeImplicit)

function(install_usdimaging_arnold_pluginfo LIB_PATH PLUGINFO CONFIG_ROOT)
    # LIB_PATH is used in the plugInfo.json.in, do not rename
    set(LIB_EXTENSION ${CMAKE_SHARED_LIBRARY_SUFFIX})
    foreach (each ${ARNOLD_USDIMAGING_CLASSES})
        set(REGISTER_ARNOLD_TYPES "${REGISTER_ARNOLD_TYPES}\n\"UsdImagingArnold${each}Adapter\":{\"bases\":[\"UsdImagingGprimAdapter\"],\"primTypeName\":\"Arnold${each}\"},")
    endforeach ()
    configure_file(
        "${USDIMAGINGARNOLD_PLUGINFO_SRC}"
        "${PLUGINFO}"
    )
    install(FILES "${PLUGINFO}"
        DESTINATION "${CONFIG_ROOT}/usdImagingArnold/resources")
endfunction()