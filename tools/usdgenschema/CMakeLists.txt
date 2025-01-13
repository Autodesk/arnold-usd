set(SRC main.cpp)


add_executable(usdGenSchemaArnold ${SRC})

add_common_includes(TARGET_NAME usdGenSchemaArnold DEPENDENCIES vt;arch;usd;usdShade;tf)

# This copies how the translator is linking against USD.
# TODO: we should make a function for this. The difference is the target and the visibility
if (BUILD_WITH_USD_STATIC)
    set(_staticlibs ${USD_usd_m_LIBRARY})
    list(APPEND _staticlibs ${USD_TRANSITIVE_STATIC_LIBS})
    list(REMOVE_DUPLICATES _staticlibs)
    if (WIN32)
        foreach(USD_M_LIB ${_staticlibs})
            if(EXISTS ${USD_M_LIB})
                target_link_options(usdGenSchemaArnold PRIVATE "-WHOLEARCHIVE:${USD_M_LIB}")
            else()
                target_link_libraries(usdGenSchemaArnold PRIVATE ${USD_M_LIB})
            endif()
        endforeach()
    else ()
        list(JOIN _staticlibs "," _staticlibs)
        if (LINUX)
            target_link_libraries(usdGenSchemaArnold PRIVATE dl pthread)
            target_link_options(usdGenSchemaArnold PRIVATE "-Wl,--whole-archive,${_staticlibs},--no-whole-archive")
        else () # APPLE
            target_link_options(usdGenSchemaArnold PRIVATE "-Wl,-all_load,${_staticlibs}")
        endif ()
    endif ()
else () # USD shared lib build
    if (USD_MONOLITHIC_BUILD)
        target_link_libraries(usdGenSchemaArnold PRIVATE usd_ms)
    else ()
        if (LINUX)
            target_link_libraries(usdGenSchemaArnold PRIVATE dl pthread)
        endif ()
        target_link_libraries(usdGenSchemaArnold PRIVATE vt;arch;usd;usdShade;tf)
    endif ()
endif ()

# When usdGenSchemaArnold is compiled with the render delegate, it needs its usd configuration to be able to run,
# we copy it regardless of the build type
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/usd"
    COMMAND
    ${CMAKE_COMMAND} -E copy_directory "${USD_LIBRARY_DIR}/usd" "$<TARGET_FILE_DIR:usdGenSchemaArnold>/usd"
)

add_custom_target(usdGenSchemaConfigs
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/usd"
)
add_dependencies(usdGenSchemaArnold usdGenSchemaConfigs)