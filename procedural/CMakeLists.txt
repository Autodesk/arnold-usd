set(SRC
        main.cpp)

add_library(usd_proc SHARED ${SRC})
target_link_libraries(usd_proc PRIVATE translator)
target_include_directories(usd_proc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../translator/reader")
target_include_directories(usd_proc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../translator/writer")
set_target_properties(usd_proc PROPERTIES PREFIX "")

if (EXISTS "${ARNOLD_INCLUDE_DIR}/ai_scene_format.h")
    target_compile_definitions(usd_proc PRIVATE ARNOLD_HAS_SCENE_FORMAT_API)
endif ()

if (APPLE OR LINUX)
    target_link_libraries(usd_proc PRIVATE dl)
endif ()

# Hiding exported symbols
if (LINUX)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/plugin.map -fvisibility=hidden")
elseif(APPLE)
    if (${ARNOLD_VERSION} VERSION_GREATER_EQUAL "6.0.3.0")
        string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Xlinker -S -Xlinker -x -Xlinker -exported_symbols_list -Xlinker ${CMAKE_CURRENT_SOURCE_DIR}/macos_export_list")
    else ()
        string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Xlinker -S -Xlinker -x -Xlinker -exported_symbols_list -Xlinker ${CMAKE_CURRENT_SOURCE_DIR}/macos_export_list_no_scene")
    endif ()
endif ()

install(TARGETS usd_proc
        DESTINATION "${PREFIX_PROCEDURAL}")

if (USD_STATIC_BUILD)
    # When linking statically we need to install the plugInfos for the included files.
    install(DIRECTORY "${USD_LIBRARY_DIR}/usd"
            DESTINATION "${PREFIX_PROCEDURAL}")
endif ()