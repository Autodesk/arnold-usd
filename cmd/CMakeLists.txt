set(SRC
        main.cpp)

add_executable(arnold_to_usd ${SRC})
target_link_libraries(arnold_to_usd PRIVATE translator)
target_include_directories(arnold_to_usd PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../translator/writer")

install(TARGETS arnold_to_usd
        DESTINATION "${PREFIX_BIN}")