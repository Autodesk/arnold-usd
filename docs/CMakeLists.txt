find_package(Doxygen REQUIRED)

set(DOXYGEN_IN "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
set(DOXYGEN_OUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

set(OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out")
configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

add_custom_target(docs_doxygen ALL
                  DEPENDS
                  ${CMAKE_CURRENT_SOURCE_DIR}/../README.md
                  ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
                  ${CMAKE_CURRENT_SOURCE_DIR}/conventions.md
                  ${CMAKE_CURRENT_SOURCE_DIR}/documenting.md
                  COMMAND ${DOXYGEN_EXECUTABLE} "${DOXYGEN_OUT}"
                  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
                  COMMENT "Generating API documentation with Doxygen"
                  VERBATIM)
add_dependencies(build_all docs_doxygen)

install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out/html"
        DESTINATION docs)