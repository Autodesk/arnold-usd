if (BUILD_PROCEDURAL)

set(SRC
    main.cpp)

set(PROC_NAME "${USD_PROCEDURAL_NAME}_proc")

add_library(${PROC_NAME} SHARED ${SRC})
set_target_properties(${PROC_NAME} PROPERTIES PREFIX "")


target_include_directories(${PROC_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/translator/reader")
target_include_directories(${PROC_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/translator/writer")

target_compile_definitions(${PROC_NAME} PRIVATE USD_PROCEDURAL_NAME=${USD_PROCEDURAL_NAME})

if (EXISTS "${ARNOLD_INCLUDE_DIR}/ai_scene_format.h" AND BUILD_PROC_SCENE_FORMAT)
    target_compile_definitions(${PROC_NAME} PRIVATE ARNOLD_HAS_SCENE_FORMAT_API)
endif ()


if (LINUX)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/plugin.map -Wl,--exclude-libs=ALL")
endif ()
# Hiding exported symbols
if (LINUX)
    target_compile_options(${PROC_NAME} PRIVATE -fvisibility=hidden)
elseif (APPLE)
    if (${ARNOLD_VERSION} VERSION_GREATER_EQUAL "6.0.3.0")
        if (USD_STATIC_BUILD)
            string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Xlinker -S -Xlinker -x -Xlinker -exported_symbols_list -Xlinker ${CMAKE_CURRENT_SOURCE_DIR}/macos_export_list")
        else ()    
            string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Xlinker -S -Xlinker -x -Xlinker -exported_symbols_list -Xlinker ${CMAKE_CURRENT_SOURCE_DIR}/macos_export_list_shared")
        endif ()
    else ()
        string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Xlinker -S -Xlinker -x -Xlinker -exported_symbols_list -Xlinker ${CMAKE_CURRENT_SOURCE_DIR}/macos_export_list_no_scene")
    endif ()
endif ()

target_link_libraries(${PROC_NAME} PRIVATE translator common)
if (APPLE OR LINUX)
    target_link_libraries(${PROC_NAME} PRIVATE dl)
endif ()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})

install(TARGETS ${PROC_NAME}
    DESTINATION "${PREFIX_PROCEDURAL}")

if (USD_STATIC_BUILD)
    # When linking statically we need to install the plugInfos for the included files.
    install(DIRECTORY "${USD_LIBRARY_DIR}/usd"
            DESTINATION "${PREFIX_PROCEDURAL}")
endif ()
endif () 
