# - Arnold finder module
# This module searches for a valid Arnold installation.
#
# Variables that will be defined:
# ARNOLD_FOUND              Defined if a Arnold installation has been detected
# ARNOLD_LIBRARY            Path to ai library (for backward compatibility)
# ARNOLD_LIBRARIES          Path to ai library
# ARNOLD_INCLUDE_DIR        Path to the include directory (for backward compatibility)
# ARNOLD_INCLUDE_DIRS       Path to the include directory
# ARNOLD_KICK               Path to kick
# ARNOLD_PYKICK             Path to pykick
# ARNOLD_MAKETX             Path to maketx
# ARNOLD_OSLC               Path to the osl compiler
# ARNOLD_OSL_HEADER_DIR     Path to the osl headers include directory (for backward compatibility)
# ARNOLD_OSL_HEADER_DIRS    Path to the osl headers include directory
# ARNOLD_VERSION_ARCH_NUM   Arch version of Arnold
# ARNOLD_VERSION_MAJOR_NUM  Major version of Arnold
# ARNOLD_VERSION_MINOR_NUM  Minor version of Arnold
# ARNOLD_VERSION_FIX        Fix version of Arnold
# ARNOLD_VERSION            Version of Arnold
# arnold_compile_osl        Function to compile / install .osl files
#   QUIET                   Quiet builds
#   VERBOSE                 Verbose builds
#   INSTALL                 Install compiled files into DESTINATION
#   INSTALL_SOURCES         Install sources into DESTINATION_SOURCES
#   OSLC_FLAGS              Extra flags for OSLC
#   DESTINATION             Destination for compiled files
#   DESTINATION_SOURCES     Destination for source files
#   INCLUDES                Include directories for oslc
#   SOURCES                 Source osl files
#
#
# Naming convention:
#  Local variables of the form _arnold_foo
#  Input variables from CMake of the form ARNOLD_FOO
#  Output variables of the form ARNOLD_FOO
#

if (EXISTS "$ENV{ARNOLD_LOCATION}")
    set(ARNOLD_LOCATION $ENV{ARNOLD_LOCATION})
endif ()

if (WIN32)
    set(EXECUTABLE_SUFFIX ".exe")
else ()
    set(EXECUTABLE_SUFFIX "")
endif ()

find_library(ARNOLD_LIBRARY
             NAMES ai
             PATHS ${ARNOLD_LOCATION}/bin
                   ${ARNOLD_LOCATION}/lib
             DOC "Arnold library")

find_file(ARNOLD_KICK
          names kick${EXECUTABLE_SUFFIX}
          PATHS "${ARNOLD_LOCATION}/bin"
          DOC "Arnold kick executable")

find_path(ARNOLD_BINARY_DIR kick${EXECUTABLE_SUFFIX}
          PATHS "${ARNOLD_LOCATION}/bin"
          DOC Path to arnold binaries)

find_file(ARNOLD_MAKETX
          names maketx${EXECUTABLE_SUFFIX}
          PATHS "${ARNOLD_LOCATION}/bin"
          DOC "Arnold maketx executable")

find_file(ARNOLD_OIIOTOOL
          names maketx${EXECUTABLE_SUFFIX}
          PATHS "${ARNOLD_LOCATION}/bin"
          DOC "Arnold maketx executable")

find_path(ARNOLD_INCLUDE_DIR ai.h
          PATHS "${ARNOLD_LOCATION}/include"
          DOC "Arnold include path")

find_path(ARNOLD_PYTHON_DIR arnold/ai_array.py
          PATHS "${ARNOLD_LOCATION}/python"
          DOC "Arnold python directory path")

set(ARNOLD_LIBRARIES ${ARNOLD_LIBRARY})
set(ARNOLD_INCLUDE_DIRS ${ARNOLD_INCLUDE_DIR})
set(ARNOLD_PYTHON_DIRS ${ARNOLD_PYTHON_DIR})
set(ARNOLD_OSL_HEADER_DIRS ${ARNOLD_OSL_HEADER_DIR})

if(ARNOLD_INCLUDE_DIR AND EXISTS "${ARNOLD_INCLUDE_DIR}/ai_version.h")
    foreach(_arnold_comp ARCH_NUM MAJOR_NUM MINOR_NUM FIX)
        file(STRINGS
             ${ARNOLD_INCLUDE_DIR}/ai_version.h
             _arnold_tmp
             REGEX "#define AI_VERSION_${_arnold_comp} .*$")
        string(REGEX MATCHALL "[0-9]+" ARNOLD_VERSION_${_arnold_comp} ${_arnold_tmp})
    endforeach()
    set(ARNOLD_VERSION ${ARNOLD_VERSION_ARCH_NUM}.${ARNOLD_VERSION_MAJOR_NUM}.${ARNOLD_VERSION_MINOR_NUM}.${ARNOLD_VERSION_FIX})
endif()

message(STATUS "Arnold library: ${ARNOLD_LIBRARY}")
message(STATUS "Arnold headers: ${ARNOLD_INCLUDE_DIR}")
message(STATUS "Arnold version: ${ARNOLD_VERSION}")

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Arnold
                                  REQUIRED_VARS
                                  ARNOLD_BINARY_DIR
                                  ARNOLD_LIBRARY
                                  ARNOLD_INCLUDE_DIR
                                  ARNOLD_KICK
                                  ARNOLD_MAKETX
                                  VERSION_VAR
                                  ARNOLD_VERSION)
