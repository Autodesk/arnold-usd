set(SRC main.cpp)

add_executable(turntable ${SRC})

add_custom_command(TARGET turntable POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/hdri"
            "$<TARGET_FILE_DIR:turntable>/hdri")

target_include_directories(turntable PRIVATE
    "${USD_INCLUDE_DIR}"
    "${USD_TRANSITIVE_INCLUDE_DIRS}"
    "${TBB_INCLUDE_DIRS}"
    "${Boost_INCLUDE_DIRS}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Mirror the static/shared linking pattern used by usdgenschema and the translator.
if (BUILD_WITH_USD_STATIC)
    set(_staticlibs ${USD_usd_m_LIBRARY})
    list(APPEND _staticlibs ${USD_TRANSITIVE_STATIC_LIBS})
    list(REMOVE_DUPLICATES _staticlibs)
    if (WIN32)
        foreach(USD_M_LIB ${_staticlibs})
            if(EXISTS ${USD_M_LIB})
                target_link_options(turntable PRIVATE "-WHOLEARCHIVE:${USD_M_LIB}")
            else()
                target_link_libraries(turntable PRIVATE ${USD_M_LIB})
            endif()
        endforeach()
    else ()
        list(JOIN _staticlibs "," _staticlibs)
        if (LINUX)
            target_link_libraries(turntable PRIVATE dl pthread)
            target_link_options(turntable PRIVATE "-Wl,--whole-archive,${_staticlibs},--no-whole-archive")
        else () # APPLE
            target_link_options(turntable PRIVATE "-Wl,-all_load,${_staticlibs}")
        endif ()
    endif ()
else () # USD shared lib build
    if (USD_MONOLITHIC_BUILD)
        target_link_libraries(turntable PRIVATE usd_ms)
    else ()
        if (LINUX)
            target_link_libraries(turntable PRIVATE dl pthread)
        endif ()
        target_link_libraries(turntable PRIVATE tf;sdf;usd;usdGeom;usdLux;usdRender;usdShade)
    endif ()
endif ()

install(TARGETS turntable DESTINATION "${PREFIX_BIN}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/hdri"
    DESTINATION "${PREFIX_BIN}")

if (BUILD_WITH_USD_STATIC)
    install(DIRECTORY "${USD_LIBRARY_DIR}/usd"
            DESTINATION "${PREFIX_BIN}")
    # Also install USD_LOCATION/plugin/usd (contains adskAssetResolver and other USD-distribution plugins).
    if (EXISTS "${USD_LOCATION}/plugin/usd")
        install(DIRECTORY "${USD_LOCATION}/plugin/usd"
                DESTINATION "${PREFIX_BIN}")
    endif ()
endif ()
