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
        writer/write_shader.cpp
        writer/writer.cpp)

add_library(translator STATIC ${SRC})
add_dependencies(build_all translator)
target_include_directories(translator SYSTEM PUBLIC "${USD_INCLUDE_DIR}")
target_include_directories(translator SYSTEM PUBLIC "${ARNOLD_INCLUDE_DIR}")
target_include_directories(translator SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
target_include_directories(translator SYSTEM PUBLIC "${TBB_INCLUDE_DIRS}")
target_include_directories(translator SYSTEM PUBLIC "${PYTHON_INCLUDE_DIRS}")
target_include_directories(translator PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(translator PUBLIC "${ARNOLD_LIBRARY}" "${PYTHON_LIBRARY}")
target_link_libraries(translator PUBLIC "${Boost_LIBRARIES}" "${TBB_LIBRARIES}")
if (PXR_MONOLITHIC_BUILD)
    target_link_libraries(translator PUBLIC usd_ms)
else ()
    target_link_libraries(translator PUBLIC gf sdf tf usd ar usdGeom usdShade vt usdLux)
endif ()

if (NOT WIN32)
    set_target_properties(translator PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()