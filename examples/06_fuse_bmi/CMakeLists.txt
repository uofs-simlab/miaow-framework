# Load helpers for creating plugins and passing their paths to the executable
include("${PROJECT_SOURCE_DIR}/cmake/miaow_dynamic_bmi_plugin.cmake")

# Build the FUSE BMI shared library plugin
include("${CMAKE_CURRENT_LIST_DIR}/cmake/fuse_plugin.cmake")

# Build the driver executable
add_executable(miaow_fuse_bmi main.cpp)
target_compile_features(miaow_fuse_bmi PRIVATE cxx_std_20)
target_link_libraries(miaow_fuse_bmi PRIVATE miaow)

# Embed the config directory into the binary
target_compile_definitions(miaow_fuse_bmi PRIVATE
    MIAOW_FUSE_CONFIG_DIR="${CMAKE_CURRENT_LIST_DIR}/data/configs"
)

miaow_attach_plugin_paths(miaow_fuse_bmi
    MIAOW_FUSE_PLUGIN_PATH miaow_dynamic_fuse_plugin
)

set_target_properties(miaow_fuse_bmi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)
