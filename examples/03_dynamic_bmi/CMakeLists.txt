cmake_minimum_required(VERSION 3.21)

# If using a standalone build for this example, enable the required languages
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(miaow_dynamic_bmi_example LANGUAGES CXX Fortran)
endif()

# Load MIAOW and choose the executable name for this example
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/miaow_setup_example.cmake")
miaow_setup_example(dynamic_bmi)

# Load helpers for creating plugins and passing their paths to the executable
include("${MIAOW_ROOT}/cmake/miaow_dynamic_bmi_plugin.cmake")

# Allow local model checkouts to override the default third_party sources
set(MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT "" CACHE PATH
    "Path to a local csdms/bmi-example-cxx source tree")
set(MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT "" CACHE PATH
    "Path to a local csdms/bmi-example-fortran source tree")

# Locate and build each language-specific BMI plugin
include("${CMAKE_CURRENT_LIST_DIR}/cmake/cpp_heat_plugin.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/fortran_heat_plugin.cmake")

# Build the driver that submits tasks using both plugins
add_executable(${EXECUTABLE_TARGET}
    main.cpp
)

target_compile_features(${EXECUTABLE_TARGET} PRIVATE cxx_std_20)
target_link_libraries(${EXECUTABLE_TARGET} PRIVATE miaow)
target_compile_definitions(${EXECUTABLE_TARGET} PRIVATE
    MIAOW_DYNAMIC_BMI_RUN_DIR="${CMAKE_CURRENT_LIST_DIR}"
)

# Compile the final plugin filenames into the driver and add build dependencies
miaow_attach_plugin_paths(${EXECUTABLE_TARGET}
    MIAOW_CPP_HEAT_PLUGIN_PATH      miaow_dynamic_cpp_heat_plugin
    MIAOW_FORTRAN_HEAT_PLUGIN_PATH  miaow_dynamic_fortran_heat_plugin
)

# Place the binary in the example directory for source-relative execution
set_target_properties(${EXECUTABLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)

if(BUILD_TESTING)
    miaow_add_example_smoke_test(
        TEST_NAME miaow_example_03_dynamic_bmi
        EXECUTABLE_TARGET "${EXECUTABLE_TARGET}"
        EXTERNAL_MODEL
    )
endif()
