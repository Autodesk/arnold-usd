set(SRC
    renderOptions.h
    renderOptions.cpp
    renderer.h
    renderer.cpp
    privateSceneDelegate.cpp
    privateSceneDelegate.h
    main.cpp)

add_executable(mock_hydra ${SRC})
add_common_dependencies(
    TARGET_NAME mock_hydra
    USD_DEPENDENCIES arch tf vt gf work hf hd sdf usd usdImaging cameraUtil usdGeom trace hdx hio hdSt )

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC})

install(TARGETS mock_hydra
    DESTINATION "${PREFIX_BIN}")
