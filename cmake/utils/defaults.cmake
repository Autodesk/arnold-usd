
# In Xcode and VisualC++ this will allow to organise the files following the folder structure
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif ()

if (LINUX)
    add_compile_definitions(_LINUX)
elseif (APPLE)
    add_compile_definitions(_DARWIN)
else ()
    add_compile_definitions(_WINDOWS _WIN32 WIN32)
    add_compile_definitions(_WIN64)
    add_link_options(/DEBUG)
endif ()

# Compilation options specific to USD
set(CMAKE_CXX_STANDARD 14 CACHE STRING "CMake CXX Standard")

# TBB
add_compile_definitions(TBB_SUPPRESS_DEPRECATED_MESSAGES)

# Set position independent code for all sub projects
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set compiler specific flags
if (CMAKE_COMPILER_IS_GNUCXX)
    include(gccdefaults)
elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    include(clangdefaults)
elseif(MSVC)
    include(msvcdefaults)
endif()

