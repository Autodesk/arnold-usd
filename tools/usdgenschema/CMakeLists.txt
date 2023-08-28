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
        get_property(USD_usd_m_LIBRARY TARGET usd_m PROPERTY INTERFACE_LINK_LIBRARIES)
        target_link_options(usdGenSchemaArnold PRIVATE "-WHOLEARCHIVE:${USD_usd_m_LIBRARY},${USD_TRANSITIVE_STATIC_LIBS}")
        # TODO: double check on windows I think we should have a -WHOLEARCHIVE: for each lib
        target_link_libraries(usdGenSchemaArnold PRIVATE Ws2_32 Dbghelp Shlwapi advapi32)
    else ()
        list(JOIN _staticlibs "," _staticlibs)
        if (LINUX)
            target_link_libraries(usdGenSchemaArnold PRIVATE dl pthread)
            target_link_options(usdGenSchemaArnold PRIVATE "-Wl,--whole-archive,${_staticlibs},--no-whole-archive")
        else () # APPLE
            target_link_options(usdGenSchemaArnold PRIVATE "-Wl,-all_load,${_staticlibs},-noall_load")
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

# Also set the usdGenSchemaArnold to 