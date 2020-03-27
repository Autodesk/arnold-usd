# Simple module to find Google Test.

if (WIN32)
    set(GOOGLETEST_LIB_EXTENSION ".lib"
            CACHE STRING "Extension of Google Test libraries")
else () # MacOS and Linux
    set(GOOGLETEST_LIB_EXTENSION ".a"
            CACHE STRING "Extension of Google Test libraries")
endif ()

if (WIN32)
    set(GOOGLETEST_LIB_PREFIX ""
            CACHE STRING "Prefix of Google Test libraries")
else ()
    set(GOOGLETEST_LIB_PREFIX "lib"
            CACHE STRING "Prefix of Google Test libraries")
endif ()


find_path(GTEST_INCLUDE_DIR gtest/gtest.h
          PATHS "${GOOGLETEST_LOCATION}/include"
                "$ENV{GOOGLETEST_LOCATION}/include"
                "${GOOGLETEST_INCLUDEDIR}"
                "$ENV{GOOGLETEST_INCLUDEDIR}"
          DOC "Google Test include directory")

find_library(GMOCK_LIBRARY ${GOOGLETEST_LIB_PREFIX}gmock${GOOGLETEST_LIB_EXTENSION}
             PATHS "${GOOGLETEST_LOCATION}/lib64"
                   "$ENV{GOOGLETEST_LOCATION}/lib64"
                   "${GOOGLETEST_LOCATION}/lib"
                   "$ENV{GOOGLETEST_LOCATION}/libb"
                   "${GOOGLETEST_LIBRARYDIR}"
                   "$ENV{GOOGLETEST_LIBRARYDIR}"
             DOC "Google Test Mock library")

find_library(GMOCK_MAIN_LIBRARY ${GOOGLETEST_LIB_PREFIX}gmock_main${GOOGLETEST_LIB_EXTENSION}
             PATHS "${GOOGLETEST_LOCATION}/lib64"
                    "$ENV{GOOGLETEST_LOCATION}/lib64"
                    "${GOOGLETEST_LOCATION}/lib"
                    "$ENV{GOOGLETEST_LOCATION}/libb"
                    "${GOOGLETEST_LIBRARYDIR}"
                    "$ENV{GOOGLETEST_LIBRARYDIR}"
             DOC "Google Test Mock Main library")

find_library(GTEST_LIBRARY ${GOOGLETEST_LIB_PREFIX}gtest${GOOGLETEST_LIB_EXTENSION}
             PATHS "${GOOGLETEST_LOCATION}/lib64"
                    "$ENV{GOOGLETEST_LOCATION}/lib64"
                    "${GOOGLETEST_LOCATION}/lib"
                    "$ENV{GOOGLETEST_LOCATION}/libb"
                    "${GOOGLETEST_LIBRARYDIR}"
                    "$ENV{GOOGLETEST_LIBRARYDIR}"
             DOC "Google Test library")

find_library(GTEST_MAIN_LIBRARY ${GOOGLETEST_LIB_PREFIX}gtest_main${GOOGLETEST_LIB_EXTENSION}
             PATHS "${GOOGLETEST_LOCATION}/lib64"
                   "$ENV{GOOGLETEST_LOCATION}/lib64"
                   "${GOOGLETEST_LOCATION}/lib"
                   "$ENV{GOOGLETEST_LOCATION}/libb"
                   "${GOOGLETEST_LIBRARYDIR}"
                   "$ENV{GOOGLETEST_LIBRARYDIR}"
             DOC "Google Test Mock Main library")

set(GTEST_LIBRARIES "${GMOCK_LIBRARY};${GMOCK_MAIN_LIBRARY};${GTEST_LIBRARY};${GTEST_MAIN_LIBRARY}")

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(GoogleTest
    REQUIRED_VARS
        GTEST_INCLUDE_DIR
        GMOCK_LIBRARY
        GMOCK_MAIN_LIBRARY
        GTEST_LIBRARY
        GTEST_MAIN_LIBRARY)