set(SRC
    renderOptions.h
    renderOptions.cpp
    renderer.h
    renderer.cpp
    main.cpp)

add_executable(riddick ${SRC})
add_common_dependencies(
    TARGET_NAME riddick
    USD_DEPENDENCIES arch tf vt gf work hf hd sdf usd usdImaging cameraUtil usdGeom trace hdx hio hdSt )

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC})

install(TARGETS riddick
    DESTINATION "${PREFIX_BIN}")
