cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_fuse_bmi_example LANGUAGES CXX Fortran)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(fuse_bmi)

include("${MIAOW_ROOT}/cmake/miaow_dynamic_bmi_plugin.cmake")

# Build the FUSE BMI shared library plugin
include("${CMAKE_CURRENT_LIST_DIR}/cmake/fuse_plugin.cmake")

# Build the driver executable
add_executable(${EXECUTABLE_TARGET} main.cpp)
target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_20)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)

# Embed the plugin path and config directory into the binary
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_FUSE_CONFIG_DIR="${CMAKE_CURRENT_LIST_DIR}/data/configs"
)

miaow_attach_plugin_paths(${EXECUTABLE_TARGET}
    MIAOW_FUSE_PLUGIN_PATH miaow_dynamic_fuse_plugin
)

set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)
