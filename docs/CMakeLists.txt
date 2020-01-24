find_package(Doxygen REQUIRED)

set(DOXYGEN_IN "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
set(DOXYGEN_OUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

set(OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out")
configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

set(DOXYGEN_INDEX "${CMAKE_CURRENT_BINARY_DIR}/out/html/index.html")

file(GLOB_RECURSE DELEGATE_HDRS CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/render_delegate/*.h")

file(GLOB_RECURSE NDR_HDRS CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/ndr/*.h")

file(GLOB_RECURSE TRANSLATOR_HDRS CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/translator/*.h")

add_custom_command(OUTPUT ${DOXYGEN_INDEX}
                   DEPENDS
                   "${CMAKE_CURRENT_SOURCE_DIR}/../README.md"
                   "${CMAKE_CURRENT_SOURCE_DIR}/building.md"
                   "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile"
                   "${CMAKE_CURRENT_SOURCE_DIR}/conventions.md"
                   "${CMAKE_CURRENT_SOURCE_DIR}/documenting.md"
                   "${CMAKE_CURRENT_SOURCE_DIR}/naming_conventions.md"
                   ${DELEGATE_HDRS}
                   ${NDR_HDRS}
                   ${TRANSLATOR_HDRS}
                   COMMAND ${DOXYGEN_EXECUTABLE} "${DOXYGEN_OUT}"
                   WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
                   COMMENT "Generating API documentation with Doxygen"
                   VERBATIM)
add_custom_target(docs_doxygen ALL
                  DEPENDS
                  ${DOXYGEN_INDEX})

install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out/html"
        DESTINATION docs)
