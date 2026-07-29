# Load helpers for creating plugins and passing their paths to the executable
include("${PROJECT_SOURCE_DIR}/cmake/miaow_dynamic_bmi_plugin.cmake")

# Allow local model checkouts to override the default third_party sources
set(MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT "" CACHE PATH
    "Path to a local csdms/bmi-example-cxx source tree")
set(MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT "" CACHE PATH
    "Path to a local csdms/bmi-example-fortran source tree")

# Locate and build each language-specific BMI plugin
include("${CMAKE_CURRENT_LIST_DIR}/cmake/cpp_heat_plugin.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/fortran_heat_plugin.cmake")

# Build the driver that submits tasks using both plugins
add_executable(miaow_dynamic_bmi
    main.cpp
)

target_compile_features(miaow_dynamic_bmi PRIVATE cxx_std_20)
target_link_libraries(miaow_dynamic_bmi PRIVATE miaow)
target_compile_definitions(miaow_dynamic_bmi PRIVATE
    MIAOW_DYNAMIC_BMI_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

# Compile the final plugin filenames into the driver and add build dependencies
miaow_attach_plugin_paths(miaow_dynamic_bmi
    MIAOW_CPP_HEAT_PLUGIN_PATH      miaow_dynamic_cpp_heat_plugin
    MIAOW_FORTRAN_HEAT_PLUGIN_PATH  miaow_dynamic_fortran_heat_plugin
)

# Place the binary in the example directory for source-relative execution
set_target_properties(miaow_dynamic_bmi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_smoke_test(
        TEST_NAME miaow_example_03_dynamic_bmi
        EXECUTABLE_TARGET miaow_dynamic_bmi
        EXTERNAL_MODEL
    )
endif()
