set(SRC
        main.cpp)

add_library(usd_proc SHARED ${SRC})
add_dependencies(build_all usd_proc)
target_link_libraries(usd_proc PRIVATE translator)
target_include_directories(usd_proc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../translator/reader")
set_target_properties(usd_proc PROPERTIES PREFIX "")

install(TARGETS usd_proc
        DESTINATION "${PREFIX_PROCEDURAL}")