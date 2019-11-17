set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE ndrAi)

# required for pxr_shared_library
if (NOT TARGET shared_libs)
    add_custom_target(shared_libs)
endif ()
if (NOT TARGET python)
    add_custom_target(python ALL)
endif ()

pxr_plugin(ndrAi
    LIBRARIES
        ${PYTHON_LIBRARY}
        ${Boost_LIBRARIES}
        tf
        ndr
        sdr
        sdf
        usd
        usdAi

    INCLUDE_DIRS
        ${ARNOLD_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
        ${PYTHON_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib

    PUBLIC_CLASSES
        aiDiscovery
        aiParser

    PRIVATE_CLASSES
        utils

    PUBLIC_HEADERS
        api.h

    PYTHON_CPPFILES
        moduleDeps.cpp
        wrapAiDiscovery.cpp
        wrapAiParser.cpp

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json
)
