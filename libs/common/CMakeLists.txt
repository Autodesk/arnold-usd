include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
set(COMMON_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/common_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/constant_strings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/materials_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/parameters_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rendersettings_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/procedural_reader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shape_utils.cpp")

set(COMMON_HDR
    "${CMAKE_CURRENT_SOURCE_DIR}/api_adapter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/common_bits.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/common_utils.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/constant_strings.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/materials_utils.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/parameters_utils.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/procedural_reader.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/rendersettings_utils.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shape_utils.h")

add_library(common STATIC EXCLUDE_FROM_ALL ${COMMON_SRC})

add_common_includes(TARGET_NAME common DEPENDENCIES vt;arch;usd;usdShade;tf)

