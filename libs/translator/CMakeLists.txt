set(SRC
    reader/prim_reader.cpp
    reader/read_arnold_type.cpp
    reader/read_camera.cpp
    reader/read_geometry.cpp
    reader/read_light.cpp
    reader/read_shader.cpp
    reader/read_skinning.cpp
    reader/read_options.cpp
    reader/reader.cpp
    reader/registry.cpp
    reader/utils.cpp

    writer/prim_writer.cpp
    writer/registry.cpp
    writer/write_arnold_type.cpp
    writer/write_camera.cpp
    writer/write_geometry.cpp
    writer/write_light.cpp
    writer/write_shader.cpp
    writer/writer.cpp)

set(HDR
    reader/prim_reader.h
    reader/read_arnold_type.h
    reader/read_camera.h
    reader/read_geometry.h
    reader/read_light.h
    reader/read_shader.h
    reader/read_skinning.h
    reader/read_options.h
    reader/reader.h
    reader/registry.h
    reader/utils.h

    utils/utils.h

    writer/prim_writer.h
    writer/registry.h
    writer/write_arnold_type.h
    writer/write_camera.h
    writer/write_geometry.h
    writer/write_light.h
    writer/write_shader.h
    writer/writer.h)

# We build a static library for the translator
add_library(translator STATIC EXCLUDE_FROM_ALL ${SRC})
if (BUILD_HEADERS_AS_SOURCES)
    target_sources(translator PRIVATE ${HDR})
endif ()

add_common_includes(TARGET_NAME translator DEPENDENCIES common)

# In the following section we set the libraries needed by translator in order to link properly.
# They are defined as interface, such that the target linking with translator will add them at link time.
target_link_libraries(translator INTERFACE common)
target_link_libraries(translator INTERFACE "${ARNOLD_LIBRARY}")

# TODO Python should really be added as a dependency of USD
# if (USD_HAS_PYTHON) # That should really be USD_NEEDS_PYTHON
#     target_link_libraries(translator INTERFACE "${Boost_LIBRARIES}")
#     if (BUILD_USE_PYTHON3)
#         target_link_libraries(translator INTERFACE Python3::Python)
#     else ()
#         target_link_libraries(translator INTERFACE Python2::Python)
#     endif ()
# endif ()

if (BUILD_WITH_USD_STATIC)
    set(_staticlibs ${USD_usd_m_LIBRARY})
    list(APPEND _staticlibs ${USD_TRANSITIVE_STATIC_LIBS})
    list(REMOVE_DUPLICATES _staticlibs)
    if (WIN32)
        get_property(USD_usd_m_LIBRARY TARGET usd_m PROPERTY INTERFACE_LINK_LIBRARIES)
        target_link_options(translator INTERFACE "-WHOLEARCHIVE:${USD_usd_m_LIBRARY},${USD_TRANSITIVE_STATIC_LIBS}")
        # TODO: double check on windows I think we should have a -WHOLEARCHIVE: for each lib
        target_link_libraries(translator INTERFACE Ws2_32 Dbghelp Shlwapi advapi32)
    else ()
        list(JOIN _staticlibs "," _staticlibs)
        if (LINUX)
            target_link_libraries(translator INTERFACE dl pthread)
            target_link_options(translator INTERFACE "-Wl,--whole-archive,${_staticlibs},--no-whole-archive")
        else () # APPLE
            target_link_options(translator INTERFACE "-Wl,-all_load,${_staticlibs}")
        endif ()
    endif ()
else () # USD shared lib build
    if (USD_MONOLITHIC_BUILD)
        target_link_libraries(translator INTERFACE usd_ms)
    else ()
        if (LINUX)
            target_link_libraries(translator INTERFACE dl pthread)
        endif ()
        target_link_libraries(translator INTERFACE gf sdf tf usd ar arch pcp vt work)
        target_link_libraries(translator INTERFACE usdGeom usdShade usdUtils usdLux usdVol usdSkel usdRender)
    endif ()
endif ()

# In static build, we don't expect libraries in USD_TRANSITIVE_SHARED_LIBS, but we set it nonetheless as depending on the client
# one might want to add shared libs
target_link_libraries(translator INTERFACE "${USD_TRANSITIVE_SHARED_LIBS}")

if (NOT WIN32)
    set_target_properties(translator PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC} ${HDR})
