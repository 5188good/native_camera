cmake_minimum_required(VERSION 3.14)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

set(PROJECT_NAME "native_camera")
project(${PROJECT_NAME} LANGUAGES CXX)

cmake_policy(VERSION 3.14...3.25)

set(PLUGIN_NAME "native_camera_plugin")

list(APPEND PLUGIN_SOURCES
  "native_camera_plugin.cpp"
  "native_camera_plugin.h"
)

add_library(${PLUGIN_NAME} SHARED
  "include/native_camera/native_camera_plugin_c_api.h"
  "native_camera_plugin_c_api.cpp"
  ${PLUGIN_SOURCES}
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_compile_options(${PLUGIN_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin)
target_link_libraries(${PLUGIN_NAME} PRIVATE mfplat mfreadwrite mfuuid ole32 oleaut32 mf)

set(native_camera_bundled_libraries
  ""
  PARENT_SCOPE
)
