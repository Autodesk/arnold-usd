# This is a test to build a bundle plugin containing the procedural and the render_delegate

# We want only 2 possible configurations:
#
# 1. Arnold usd procedural only compiled with a usd static version embedded in the plugin
#    we will call it usd_proc.dll
# 2. Arnold usd procedural and all the USD/Hydra plugins dynamically linked to a shared usd library.
#    we will call it ArnoldUsdBundle_proc.dll -> proc is because arnold can only use _proc libraries .......
#

if (NOT BUILD_WITH_USD_STATIC)

set(BUNDLE_NAME "ArnoldUsdBundle")

add_library(${BUNDLE_NAME} SHARED) # or usdbundle_proc

# should not start with "lib", so set the prefix to ""
set_target_properties(${BUNDLE_NAME} PROPERTIES PREFIX "")
target_sources(${BUNDLE_NAME} PRIVATE 
	${CMAKE_CURRENT_LIST_DIR}/../render_delegate/renderer_plugin.cpp
	${CMAKE_CURRENT_LIST_DIR}/../procedural/main.cpp
)
target_compile_definitions(${BUNDLE_NAME} PRIVATE USD_PROCEDURAL_NAME=${USD_PROCEDURAL_NAME})
if (${PROC_SCENE_FORMAT})
	target_compile_definitions(${BUNDLE_NAME} PRIVATE ARNOLD_HAS_SCENE_FORMAT_API)
endif()

target_include_directories(${BUNDLE_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/translator/reader")
target_include_directories(${BUNDLE_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/translator/writer")
target_include_directories(${BUNDLE_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/render_delegate")

target_compile_definitions(${BUNDLE_NAME} PRIVATE "HDARNOLD_EXPORTS=1")
target_compile_definitions(${BUNDLE_NAME} PUBLIC ENABLE_HYDRA_IN_USD_PROCEDURAL=1)
if (${USD_VERSION} VERSION_GREATER_EQUAL "0.25.05")
	target_compile_definitions(${BUNDLE_NAME} PUBLIC ENABLE_SCENE_INDEX=1)
endif()

target_link_libraries(${BUNDLE_NAME} PRIVATE translator common usdImaging ndrObjects usdImagingArnoldObjects)
target_link_libraries(${BUNDLE_NAME} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,render_delegate>")
if (${USD_VERSION} VERSION_GREATER_EQUAL "0.25.05")
	target_link_libraries(${BUNDLE_NAME} PRIVATE sceneIndexArnoldObjects;hdsi;usdSkelImaging;ts)
endif ()

# Install the Bundle inside a new directory to avoid any collision with other installed plugins
install(TARGETS ${BUNDLE_NAME} DESTINATION "${PREFIX_BUNDLE}")

# Install procedural usd directory as well - we shouldn't need to install this directory
#install(DIRECTORY "${USD_LIBRARY_DIR}/usd"
#		DESTINATION "${PREFIX_BUNDLE}")

# Install each bundle plugins in different directory, reusing installation code
install_ndr_arnold_pluginfo(
	../../${BUNDLE_NAME} 
	"${CMAKE_CURRENT_BINARY_DIR}/nodeRegistryArnold/resources/plugInfo.json"
	"${PREFIX_BUNDLE}")

# install the usd imaging arnold pluginfo
install_usdimaging_arnold_pluginfo(
	../../${BUNDLE_NAME}
	"${CMAKE_CURRENT_BINARY_DIR}/usdImagingArnold/resources/plugInfo.json"
	"${PREFIX_BUNDLE}")

# install the render delegate arnold pluginfo
set(LIB_EXTENSION ${CMAKE_SHARED_LIBRARY_SUFFIX})
install_hdarnold_arnold_pluginfo(
	../../${BUNDLE_NAME}
	"${CMAKE_CURRENT_BINARY_DIR}/hdArnold/resources/plugInfo.json"
	"${PREFIX_BUNDLE}")

# install the scene index if we include it
if (${USD_VERSION} VERSION_GREATER_EQUAL "0.25.05")
	install_sceneindex_arnold_pluginfo(
		../../${BUNDLE_NAME}
		"${CMAKE_CURRENT_BINARY_DIR}/sceneIndexArnold/resources/plugInfo.json"
		"${PREFIX_BUNDLE}")
endif ()

endif() # NOT BUILD_WITH_USD_STATIC
