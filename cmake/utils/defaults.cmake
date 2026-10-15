
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

if (ENABLE_SYMBOLS)
    if (MSVC)
        # /Z7 puts the debug info in each object file, so parallel compiles do not
        # write a shared .pdb. The /DEBUG above makes /INCREMENTAL and
        # /OPT:NOREF,NOICF the defaults
        add_compile_options("$<$<CONFIG:Release,MinSizeRel>:/Z7>")
        add_link_options("$<$<CONFIG:Release,MinSizeRel>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF>")
    else ()
        add_compile_options("$<$<CONFIG:Release,MinSizeRel>:-g>")
    endif ()
    if (LINUX)
        # The clang driver does not ask the linker for a build-id. Tools that pair
        # a binary with its symbol file compare it
        add_link_options("-Wl,--build-id=sha1")
    endif ()
endif ()

# Compilation options specific to USD
if (${USD_VERSION} VERSION_GREATER_EQUAL "0.23.11")
    message(STATUS "Setting C++ standard to C++17")
    set(CMAKE_CXX_STANDARD 17)
else()
    message(STATUS "Setting C++ standard to C++14")
	set(CMAKE_CXX_STANDARD 14 CACHE STRING "CMake CXX Standard")
endif()

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

