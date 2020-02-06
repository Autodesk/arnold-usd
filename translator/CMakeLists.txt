set(SRC
        reader/prim_reader.cpp
        reader/read_arnold_type.cpp
        reader/read_geometry.cpp
        reader/read_light.cpp
        reader/read_shader.cpp
        reader/reader.cpp
        reader/registry.cpp
        reader/utils.cpp

        writer/prim_writer.cpp
        writer/registry.cpp
        writer/write_arnold_type.cpp
        writer/write_geometry.cpp
        writer/write_light.cpp
        writer/write_shader.cpp
        writer/writer.cpp)

set(HDR
        reader/prim_reader.h
        reader/read_arnold_type.h
        reader/read_geometry.h
        reader/read_light.h
        reader/read_shader.h
        reader/reader.h
        reader/registry.h
        reader/utils.h

        utils/utils.h

        writer/prim_writer.h
        writer/registry.h
        writer/write_arnold_type.h
        writer/write_geometry.h
        writer/write_light.h
        writer/write_shader.h
        writer/writer.h)

add_library(translator STATIC ${SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(translator PRIVATE ${HDR})
endif ()
target_include_directories(translator SYSTEM PUBLIC "${USD_INCLUDE_DIR}")
target_include_directories(translator SYSTEM PUBLIC "${ARNOLD_INCLUDE_DIR}")
target_include_directories(translator SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
target_include_directories(translator SYSTEM PUBLIC "${TBB_INCLUDE_DIRS}")
target_include_directories(translator SYSTEM PUBLIC Python2::Python)
target_include_directories(translator PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(translator PUBLIC "${ARNOLD_LIBRARY}")
if (USD_HAS_PYTHON)
    target_link_libraries(translator PUBLIC "${Boost_LIBRARIES}" Python2::Python)
endif ()

if (USD_STATIC_BUILD)
    if (WIN32)
        target_link_options(translator PUBLIC "-WHOLEARCHIVE:${USD_usd_m_LIBRARY}")
        target_link_libraries(translator PUBLIC Ws2_32 Dbghelp Shlwapi advapi32)
        target_link_libraries(translator PUBLIC "${TBB_LIBRARIES}")
    else ()
        set(_staticlibs "${USD_usd_m_LIBRARY}")
        if (TBB_STATIC_BUILD)
            set(_staticlibs "${_staticlibs},${TBB_LIBRARIES}")
        else ()
            target_link_libraries(translator PUBLIC "${TBB_LIBRARIES}")
        endif ()
        if (LINUX)
            target_link_libraries(translator PUBLIC dl pthread)
            target_link_options(translator PUBLIC "-Wl,--whole-archive,${_staticlibs},--no-whole-archive")
        else () # APPLE
            target_link_options(translator PUBLIC "-Wl,-all_load,${_staticlibs},-noall_load")
        endif ()
    endif ()
else ()
    target_link_libraries(translator PUBLIC "${TBB_LIBRARIES}")
    if (USD_MONOLITHIC_BUILD)
        target_link_libraries(translator PUBLIC usd_ms)
    else ()
        target_link_libraries(translator PUBLIC gf sdf tf usd ar usdGeom usdShade vt usdLux)
    endif ()
endif ()

if (NOT WIN32)
    set_target_properties(translator PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()