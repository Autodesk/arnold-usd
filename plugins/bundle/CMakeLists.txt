# This is a test to build a bundle plugin containing the procedural and the render_delegate

# We want only 2 possible configurations:
#
# 1. Arnold usd procedural only compiled with a usd static version embedded in the plugin
#    we will call it usd_proc.dll
# 2. Arnold usd procedural and all the USD/Hydra plugins dynamically linked to a shared usd library.
#    we will call it ArnoldUsdBundle_proc.dll -> proc is because arnold can only use _proc libraries .......
#

if (NOT BUILD_WITH_USD_STATIC)
add_library(ArnoldUsdBundle SHARED) # or usdbundle_proc
# should not start with "lib", so set the prefix to ""
set_target_properties(ArnoldUsdBundle PROPERTIES PREFIX "")
target_sources(ArnoldUsdBundle PRIVATE 
	${CMAKE_CURRENT_LIST_DIR}/../render_delegate/renderer_plugin.cpp
	${CMAKE_CURRENT_LIST_DIR}/../procedural/main.cpp
)
target_compile_definitions(ArnoldUsdBundle PRIVATE USD_PROCEDURAL_NAME=ArnoldUsdBundle)

target_include_directories(ArnoldUsdBundle PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/translator/reader")
target_include_directories(ArnoldUsdBundle PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/translator/writer")
target_include_directories(ArnoldUsdBundle PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/render_delegate")
target_compile_definitions(ArnoldUsdBundle PRIVATE "HDARNOLD_EXPORTS=1")

if (ENABLE_HYDRA_IN_USD_PROCEDURAL)
    target_compile_definitions(ArnoldUsdBundle PUBLIC ENABLE_HYDRA_IN_USD_PROCEDURAL=1)
endif()

target_link_libraries(ArnoldUsdBundle PRIVATE translator common usdImaging ndrObjects usdImagingArnoldObjects sceneIndexArnoldObjects)
target_link_libraries(ArnoldUsdBundle PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,render_delegate>")
if (${USD_VERSION} VERSION_GREATER_EQUAL "0.25.05")
	target_link_libraries(ArnoldUsdBundle PRIVATE hdsi;usdSkelImaging;ts)
endif ()

install(TARGETS ArnoldUsdBundle DESTINATION "${PREFIX_PLUGINS}")
endif() # NOT BUILD_WITH_USD_STATIC
